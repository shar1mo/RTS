/*
 * Демонстрация ppoll() для ожидания с атомарной разблокировкой сигналов.
 *
 * Цель: Показать, как правильно и безопасно обрабатывать сигналы во время
 * блокирующего ожидания на файловых дескрипторах. Простое использование
 * poll() или read() может привести к состоянию гонки (race condition),
 * если сигнал придет до того, как блокирующий вызов начался.
 * ppoll() решает эту проблему, атомарно изменяя маску сигналов на время ожидания.
 *
 * Сценарий:
 * 1. Основной поток блокирует сигнал SIGUSR1 с помощью pthread_sigmask().
 * 2. Создается дочерний поток, который через 1 секунду посылает SIGUSR1 основному.
 * 3. Основной поток вызывает ppoll(), передавая ему маску сигналов,
 *    в которой SIGUSR1 РАЗБЛОКИРОВАН.
 * 4. ppoll() атомарно снимает блокировку с SIGUSR1 и начинает ждать.
 * 5. Когда приходит сигнал, он прерывает ppoll() (возвращается EINTR),
 *    и обработчик сигнала выполняется немедленно.
 * 6. После возврата из ppoll() исходная маска (с заблокированным SIGUSR1)
 *    автоматически восстанавливается.
 */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t signal_received = 0;

static void signal_handler(int signum) {
    (void)signum;
    signal_received = 1;
    /* write — async-signal-safe */
    const char msg[] = "Signal handler executed!\n";
    (void)write(STDOUT_FILENO, msg, sizeof(msg) - 1);
}

/* Поток, посылающий сигнал */
struct sender_arg { pthread_t target; };

static void *thread_sender(void *arg) {
    struct sender_arg *a = arg;
    printf("[SENDER] sleeping for 1 second...\n");
    sleep(1);
    printf("[SENDER] sending SIGUSR1 to main thread...\n");
    int rc = pthread_kill(a->target, SIGUSR1);
    if (rc != 0) {
        fprintf(stderr, "pthread_kill failed: %s\n", strerror(rc));
    }
    return NULL;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);

    /*1. Установка обработчика сигнала*/
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    /* Не устанавливаем SA_RESTART, чтобы ppoll прерывался EINTR */
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    /*2. Блокируем SIGUSR1 и сохраняем предыдущую маску*/
    sigset_t blocked_mask, original_mask;
    sigemptyset(&blocked_mask);
    sigaddset(&blocked_mask, SIGUSR1);
    if (pthread_sigmask(SIG_BLOCK, &blocked_mask, &original_mask) != 0) {
        perror("pthread_sigmask");
        return EXIT_FAILURE;
    }
    printf("Main thread blocked SIGUSR1.\n");

    /*3. Создаём pipe для ожидания (просто как fd)*/
    int fds[2];
    if (pipe(fds) == -1) {
        perror("pipe");
        return EXIT_FAILURE;
    }

    struct pollfd pfd = {pfd.fd = fds[0], pfd.events = POLLIN};

    /*4. Запускаем поток-отправитель*/
    pthread_t sender_tid;
    struct sender_arg sarg = { sarg.target = pthread_self() };
    if (pthread_create(&sender_tid, NULL, thread_sender, &sarg) != 0) {
        perror("pthread_create");
        close(fds[0]);
        close(fds[1]);
        return EXIT_FAILURE;
    }

    /* 5. Готовим timeout и запускаем ppoll с маской original_mask.
     * ppoll атомарно установит маску сигналов равной original_mask
     * (в которой SIGUSR1 РАЗБЛОКИРОВАН) на время ожидания. */
    printf("Calling ppoll() with unblocked signal mask, waiting for signal...\n");
    struct timespec timeout = {timeout.tv_sec = 5, timeout.tv_nsec = 0};

    int rc = ppoll(&pfd, 1, &timeout, &original_mask);

    if (rc == -1) {
        if (errno == EINTR) {
            printf("ppoll was correctly interrupted by a signal (EINTR).\n");
        } else {
            perror("ppoll failed");
        }
    } else if (rc == 0) {
        printf("ppoll timed out (no events, no signal within 5s).\n");
    } else {
        printf("ppoll returned >0 (fd ready) — unexpected in this demo.\n");
    }

    if (signal_received) {
        printf("Verified that the signal handler was executed.\n");
    } else {
        printf("Warning: signal handler was NOT executed.\n");
    }

    pthread_join(sender_tid, NULL);
    close(fds[0]);
    close(fds[1]);
    return 0;
}