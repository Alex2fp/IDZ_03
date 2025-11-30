#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>
#include <semaphore.h>
#include <mqueue.h>

#define MAX_BOLTUNS 64
#define SHM_NAME "/talker3_shared"
#define DATA_SEM "/talker3_data_sem"
#define PRINT_SEM "/talker3_print_sem"
#define MQ_NAME "/talker3_queue"
#define MQ_MSG_SIZE 256

typedef struct {
    int num_boltuns;
    int next_id;
    int busy[MAX_BOLTUNS];
    int stop_flag;
} shared_data_t;

static sem_t *data_sem = NULL;
static sem_t *print_sem = NULL;
static shared_data_t *shared = NULL;
static mqd_t mq = (mqd_t)-1;
static volatile sig_atomic_t terminate_requested = 0;

static void handle_sigint(int signo) {
    (void)signo;
    terminate_requested = 1;
    if (shared) shared->stop_flag = 1;
}

static int random_between(int min, int max) {
    if (max <= min) return min;
    return min + rand() % (max - min + 1);
}

static void broadcast(const char *fmt, ...) {
    char buffer[MQ_MSG_SIZE];
    va_list args;

    sem_wait(print_sem);
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    fflush(stdout);
    sem_post(print_sem);

    mq_send(mq, buffer, strlen(buffer) + 1, 0);
}

static int acquire_id(void) {
    sem_wait(data_sem);
    int id = shared->next_id % shared->num_boltuns;
    shared->next_id++;
    sem_post(data_sem);
    return id;
}

static void init_shared(int boltuns) {
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        exit(EXIT_FAILURE);
    }
    if (ftruncate(shm_fd, sizeof(shared_data_t)) == -1) {
        perror("ftruncate");
        exit(EXIT_FAILURE);
    }
    shared_data_t *mem = mmap(NULL, sizeof(shared_data_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (mem == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }
    memset(mem, 0, sizeof(shared_data_t));
    mem->num_boltuns = boltuns;
    munmap(mem, sizeof(shared_data_t));
    close(shm_fd);
}

static void open_shared(void) {
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        exit(EXIT_FAILURE);
    }
    if (ftruncate(shm_fd, sizeof(shared_data_t)) == -1) {
        perror("ftruncate");
        exit(EXIT_FAILURE);
    }
    shared = mmap(NULL, sizeof(shared_data_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }
    close(shm_fd);
    if (shared->num_boltuns <= 0 || shared->num_boltuns > MAX_BOLTUNS) {
        shared->num_boltuns = 5;
    }
}

static void cleanup_resources(int unlink_all) {
    if (shared) {
        munmap(shared, sizeof(shared_data_t));
        shared = NULL;
    }
    if (data_sem) {
        sem_close(data_sem);
    }
    if (print_sem) {
        sem_close(print_sem);
    }
    if (mq != (mqd_t)-1) {
        mq_close(mq);
    }
    if (unlink_all) {
        sem_unlink(DATA_SEM);
        sem_unlink(PRINT_SEM);
        mq_unlink(MQ_NAME);
        shm_unlink(SHM_NAME);
    }
}

static void run_boltun(int min_pause, int max_pause, int min_talk, int max_talk, int duration) {
    int id = acquire_id();
    srand((unsigned)time(NULL) ^ (getpid()<<16));
    broadcast("[%d] стартовал (болтунов=%d)\n", id, shared->num_boltuns);

    time_t start = time(NULL);
    while (!terminate_requested && !shared->stop_flag && (time(NULL) - start < duration)) {
        int pause = random_between(min_pause, max_pause);
        sleep(pause);
        if (terminate_requested || shared->stop_flag) break;

        int target = id;
        int attempts = 0;
        while (target == id && attempts < 4 * shared->num_boltuns) {
            target = rand() % shared->num_boltuns;
            attempts++;
        }

        sem_wait(data_sem);
        if (shared->stop_flag || shared->busy[id] || shared->busy[target] || target == id) {
            sem_post(data_sem);
            continue;
        }
        shared->busy[id] = 1;
        shared->busy[target] = 1;
        sem_post(data_sem);

        broadcast("[%d] звонит %d (пауза %d c)\n", id, target, pause);
        int talk_time = random_between(min_talk, max_talk);
        sleep(talk_time);

        sem_wait(data_sem);
        shared->busy[id] = 0;
        shared->busy[target] = 0;
        sem_post(data_sem);

        broadcast("[%d] закончил разговор с %d за %d c\n", id, target, talk_time);
    }

    broadcast("[%d] завершает работу\n", id);
    mq_send(mq, "STOP", 5, 0);
}

static void usage(const char *prog) {
    fprintf(stderr, "Использование: %s [--init N] [--cleanup] [--duration sec] [мин_пауза макс_пауза мин_разговор макс_разговор]\n", prog);
}

int main(int argc, char *argv[]) {
    int boltuns = 5;
    int do_init = 0;
    int do_cleanup = 0;
    int duration = 25;
    int min_pause = 1, max_pause = 3, min_talk = 1, max_talk = 4;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--init") == 0 && i + 1 < argc) {
            do_init = 1;
            boltuns = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--cleanup") == 0) {
            do_cleanup = 1;
        } else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            duration = atoi(argv[++i]);
        } else if (min_pause == 1 && i + 3 < argc) {
            min_pause = atoi(argv[i]);
            max_pause = atoi(argv[i + 1]);
            min_talk = atoi(argv[i + 2]);
            max_talk = atoi(argv[i + 3]);
            i += 3;
        } else {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (do_cleanup) {
        cleanup_resources(1);
        return EXIT_SUCCESS;
    }

    if (do_init) {
        init_shared(boltuns);
    }

    data_sem = sem_open(DATA_SEM, O_CREAT, 0666, 1);
    if (data_sem == SEM_FAILED) {
        perror("sem_open data");
        return EXIT_FAILURE;
    }
    print_sem = sem_open(PRINT_SEM, O_CREAT, 0666, 1);
    if (print_sem == SEM_FAILED) {
        perror("sem_open print");
        return EXIT_FAILURE;
    }

    open_shared();

    struct mq_attr attr = {0};
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = MQ_MSG_SIZE;
    mq = mq_open(MQ_NAME, O_CREAT | O_WRONLY, 0666, &attr);
    if (mq == (mqd_t)-1) {
        perror("mq_open");
        return EXIT_FAILURE;
    }

    signal(SIGINT, handle_sigint);
    run_boltun(min_pause, max_pause, min_talk, max_talk, duration);

    cleanup_resources(do_cleanup);
    return EXIT_SUCCESS;
}
