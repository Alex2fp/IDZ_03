#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <semaphore.h>
#include <string.h>
#include <stdarg.h>
#include <sys/wait.h>

#define MAX_BOLTUNS 32

typedef struct {
    int num_boltuns;
    int busy[MAX_BOLTUNS];
    int stop_flag;
    sem_t data_lock;
    sem_t print_lock;
} shared_data_t;

static volatile sig_atomic_t terminate_requested = 0;

static void handle_sigint(int signo) {
    (void)signo;
    terminate_requested = 1;
}

static int random_between(int min, int max) {
    if (max <= min) return min;
    return min + rand() % (max - min + 1);
}

static void log_message(shared_data_t *shared, const char *fmt, ...) {
    va_list args;
    sem_wait(&shared->print_lock);
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    fflush(stdout);
    sem_post(&shared->print_lock);
}

static void run_boltun(int id, shared_data_t *shared, int min_pause, int max_pause, int min_talk, int max_talk) {
    srand((unsigned)time(NULL) ^ (getpid()<<16));
    while (!terminate_requested && !shared->stop_flag) {
        int sleep_time = random_between(min_pause, max_pause);
        sleep(sleep_time);

        if (terminate_requested || shared->stop_flag) break;

        int target = id;
        int tries = 0;
        while (target == id && tries < 3 * shared->num_boltuns) {
            target = rand() % shared->num_boltuns;
            tries++;
        }

        sem_wait(&shared->data_lock);
        if (shared->stop_flag) {
            sem_post(&shared->data_lock);
            break;
        }

        if (target == id || shared->busy[target] || shared->busy[id]) {
            sem_post(&shared->data_lock);
            continue; // попробовать позже
        }

        shared->busy[target] = 1;
        shared->busy[id] = 1;
        sem_post(&shared->data_lock);

        log_message(shared, "[%d] звоню абоненту %d (ожидал %d c)\n", id, target, sleep_time);
        int talk_time = random_between(min_talk, max_talk);
        sleep(talk_time);

        sem_wait(&shared->data_lock);
        shared->busy[target] = 0;
        shared->busy[id] = 0;
        sem_post(&shared->data_lock);

        log_message(shared, "[%d] завершил разговор с %d за %d c\n", id, target, talk_time);
    }

    log_message(shared, "[%d] завершает работу\n", id);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Использование: %s <число болтунов (<=%d)> <длительность симуляции, с> [мин_ожид мин_разговор макс_разговор]\n", argv[0], MAX_BOLTUNS);
        return EXIT_FAILURE;
    }

    int n = atoi(argv[1]);
    if (n <= 0 || n > MAX_BOLTUNS) {
        fprintf(stderr, "Некорректное число болтунов\n");
        return EXIT_FAILURE;
    }

    int simulation_time = atoi(argv[2]);
    int min_pause = 1, max_pause = 3, min_talk = 1, max_talk = 4;
    if (argc >= 6) {
        min_pause = atoi(argv[3]);
        max_pause = atoi(argv[4]);
        min_talk = atoi(argv[5]);
        if (argc >= 7) {
            max_talk = atoi(argv[6]);
        }
    }

    signal(SIGINT, handle_sigint);

    int shm_fd = shm_open("/prog1_shared", O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        return EXIT_FAILURE;
    }
    if (ftruncate(shm_fd, sizeof(shared_data_t)) == -1) {
        perror("ftruncate");
        return EXIT_FAILURE;
    }

    shared_data_t *shared = mmap(NULL, sizeof(shared_data_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared == MAP_FAILED) {
        perror("mmap");
        return EXIT_FAILURE;
    }

    memset(shared, 0, sizeof(shared_data_t));
    shared->num_boltuns = n;
    sem_init(&shared->data_lock, 1, 1);
    sem_init(&shared->print_lock, 1, 1);

    pid_t *pids = calloc(n, sizeof(pid_t));
    if (!pids) {
        perror("calloc");
        return EXIT_FAILURE;
    }

    for (int i = 0; i < n; ++i) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            return EXIT_FAILURE;
        }
        if (pid == 0) {
            run_boltun(i, shared, min_pause, max_pause, min_talk, max_talk);
            return EXIT_SUCCESS;
        }
        pids[i] = pid;
    }

    time_t start = time(NULL);
    while (!terminate_requested && (time(NULL) - start < simulation_time)) {
        sleep(1);
    }

    sem_wait(&shared->data_lock);
    shared->stop_flag = 1;
    sem_post(&shared->data_lock);

    for (int i = 0; i < n; ++i) {
        kill(pids[i], SIGINT);
    }

    for (int i = 0; i < n; ++i) {
        waitpid(pids[i], NULL, 0);
    }

    sem_destroy(&shared->data_lock);
    sem_destroy(&shared->print_lock);
    munmap(shared, sizeof(shared_data_t));
    close(shm_fd);
    shm_unlink("/prog1_shared");

    free(pids);
    printf("Родитель завершил работу\n");
    return EXIT_SUCCESS;
}
