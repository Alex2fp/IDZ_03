#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <mqueue.h>
#include <string.h>
#include <time.h>

#define MQ_NAME "/talker3_queue"
#define MQ_MSG_SIZE 256

static volatile sig_atomic_t stop_requested = 0;

static void handle_sigint(int signo) {
    (void)signo;
    stop_requested = 1;
}

int main(void) {
    signal(SIGINT, handle_sigint);

    struct mq_attr attr = {0};
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = MQ_MSG_SIZE;

    mqd_t mq = mq_open(MQ_NAME, O_CREAT | O_RDONLY, 0666, &attr);
    if (mq == (mqd_t)-1) {
        perror("mq_open");
        return EXIT_FAILURE;
    }

    printf("Наблюдатель готов к приёму сообщений...\n");
    char buffer[MQ_MSG_SIZE];

    while (!stop_requested) {
        ssize_t bytes = mq_receive(mq, buffer, sizeof(buffer), NULL);
        if (bytes >= 0) {
            if (strncmp(buffer, "STOP", 4) == 0) {
                printf("Получен сигнал остановки, наблюдатель завершает работу.\n");
                break;
            }
            buffer[bytes] = '\0';
            printf("[OBS] %s", buffer);
            fflush(stdout);
        } else {
            usleep(100000);
        }
    }

    mq_close(mq);
    return EXIT_SUCCESS;
}
