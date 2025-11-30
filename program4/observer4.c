#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <semaphore.h>

#define SHM_NAME "/talker4_shared"
#define LOG_SEM "/talker4_log_sem"
#define PRINT_SEM "/talker4_print_sem"
#define LOG_CAP 256
#define LOG_LEN 180

typedef struct {
    int num_boltuns;
    int next_id;
    int busy[64];
    int stop_flag;
    unsigned long seq;
    char log_buffer[LOG_CAP][LOG_LEN];
} shared_data_t;

static volatile sig_atomic_t stop_requested = 0;

static void handle_sigint(int signo) {
    (void)signo;
    stop_requested = 1;
}

int main(void) {
    signal(SIGINT, handle_sigint);

    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
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
    close(shm_fd);

    sem_t *log_sem = sem_open(LOG_SEM, O_CREAT, 0666, 1);
    if (log_sem == SEM_FAILED) {
        perror("sem_open log");
        return EXIT_FAILURE;
    }
    sem_t *print_sem = sem_open(PRINT_SEM, O_CREAT, 0666, 1);
    if (print_sem == SEM_FAILED) {
        perror("sem_open print");
        return EXIT_FAILURE;
    }

    unsigned long last_seq = 0;
    printf("Наблюдатель подключён, текущая очередь: %lu сообщений.\n", shared->seq);

    while (!stop_requested) {
        if (shared->stop_flag && last_seq >= shared->seq) {
            break;
        }

        if (last_seq < shared->seq) {
            sem_wait(log_sem);
            while (last_seq < shared->seq) {
                unsigned long index = last_seq % LOG_CAP;
                char message[LOG_LEN];
                snprintf(message, sizeof(message), "%s", shared->log_buffer[index]);
                sem_wait(print_sem);
                printf("[OBS4] %s", message);
                fflush(stdout);
                sem_post(print_sem);
                last_seq++;
            }
            sem_post(log_sem);
        } else {
            usleep(150000);
        }
    }

    sem_close(log_sem);
    sem_close(print_sem);
    munmap(shared, sizeof(shared_data_t));
    return EXIT_SUCCESS;
}
