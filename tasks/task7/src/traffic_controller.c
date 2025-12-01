#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <sys/select.h>
#include "common.h"

// Разделяемые данные
SharedData shared_data;
timer_t timer;
volatile sig_atomic_t timer_expired = 0;

// Обработчик сигнала таймера
void timer_handler(int sig) {
    (void)sig;
    timer_expired = 1;
}

// Вывод состояния светофоров
void print_lights(TrafficState state) {
    printf("State: %d | ", state);
    switch (state) {
        case STATE_NS_GREEN:    printf("NS: GREEN, EW: RED\n"); break;
        case STATE_NS_YELLOW:   printf("NS: YELLOW, EW: RED\n"); break;
        case STATE_EW_GREEN:    printf("NS: RED, EW: GREEN\n"); break;
        case STATE_EW_YELLOW:   printf("NS: RED, EW: YELLOW\n"); break;
        case STATE_ALL_RED:     printf("NS: RED, EW: RED\n"); break;
        case STATE_PED_CROSS:   printf("NS: RED, EW: RED | WALK\n"); break;
        case STATE_EMERGENCY:   printf("EMERGENCY! NS: RED, EW: RED\n"); break;
        default:                printf("Unknown state\n"); break;
    }
    fflush(stdout);
}

// Поток контроллера (FSM)
void* controller_thread_func(void* arg) {
    (void)arg;
    TrafficState next_state = STATE_ALL_RED;

    while (1) {
        pthread_mutex_lock(&shared_data.mutex);
        shared_data.current_state = next_state;
        print_lights(shared_data.current_state);
        pthread_mutex_unlock(&shared_data.mutex);

        struct itimerspec its;
        timer_expired = 0;
        its.it_interval.tv_sec = 0;
        its.it_interval.tv_nsec = 0;

        switch (shared_data.current_state) {
            case STATE_NS_GREEN:
                its.it_value.tv_sec = GREEN_DURATION;
                next_state = STATE_NS_YELLOW;
                break;
            case STATE_NS_YELLOW:
                its.it_value.tv_sec = YELLOW_DURATION;
                next_state = STATE_EW_GREEN;
                break;
            case STATE_EW_GREEN:
                its.it_value.tv_sec = GREEN_DURATION;
                next_state = STATE_EW_YELLOW;
                break;
            case STATE_EW_YELLOW:
                its.it_value.tv_sec = YELLOW_DURATION;
                next_state = STATE_ALL_RED;
                break;
            case STATE_ALL_RED:
                its.it_value.tv_sec = ALL_RED_DURATION;
                if (shared_data.ped_ns_request || shared_data.ped_ew_request)
                    next_state = STATE_PED_CROSS;
                else if (shared_data.emergency_request)
                    next_state = STATE_EMERGENCY;
                else
                    next_state = STATE_NS_GREEN;
                break;
            case STATE_PED_CROSS:
                its.it_value.tv_sec = PED_CROSS_DURATION;
                pthread_mutex_lock(&shared_data.mutex);
                shared_data.ped_ns_request = 0;
                shared_data.ped_ew_request = 0;
                pthread_mutex_unlock(&shared_data.mutex);
                next_state = STATE_NS_GREEN;
                break;
            case STATE_EMERGENCY:
                its.it_value.tv_sec = ALL_RED_DURATION;
                pthread_mutex_lock(&shared_data.mutex);
                shared_data.emergency_request = 0;
                pthread_mutex_unlock(&shared_data.mutex);
                next_state = STATE_ALL_RED;
                break;
            default:
                its.it_value.tv_sec = 1;
                next_state = STATE_ALL_RED;
                break;
        }

        timer_settime(timer, 0, &its, NULL);

        struct timespec ts = {0, 10000000L}; // 10 мс
        while (!timer_expired) {
            nanosleep(&ts, NULL);
        }
    }
    return NULL;
}

// Поток обработки пользовательского ввода (без Enter)
void* input_thread_func(void* arg) {
    (void)arg;
    printf("Input keys: n (NS ped), e (EW ped), s (siren)\n");
    fflush(stdout);

    while (1) {
        fd_set set;
        struct timeval timeout = {0, 100000L}; // 0.1 ms
        FD_ZERO(&set);
        FD_SET(STDIN_FILENO, &set);

        int rv = select(STDIN_FILENO + 1, &set, NULL, NULL, &timeout);
        if (rv > 0 && FD_ISSET(STDIN_FILENO, &set)) {
            char c = getchar();
            if (c == EOF) continue;
            pthread_mutex_lock(&shared_data.mutex);
            if (c == 'n') shared_data.ped_ns_request = 1;
            if (c == 'e') shared_data.ped_ew_request = 1;
            if (c == 's') shared_data.emergency_request = 1;
            pthread_mutex_unlock(&shared_data.mutex);
        }
    }
    return NULL;
}

int main() {
    pthread_mutex_init(&shared_data.mutex, NULL);

    struct sigaction sa;
    sa.sa_handler = timer_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGRTMIN, &sa, NULL);

    struct sigevent sev;
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGRTMIN;
    sev.sigev_value.sival_ptr = &timer;
    if (timer_create(CLOCK_MONOTONIC, &sev, &timer) == -1) {
        perror("timer_create");
        return 1;
    }

    pthread_t controller_thread, input_thread;
    pthread_create(&controller_thread, NULL, controller_thread_func, NULL);
    pthread_create(&input_thread, NULL, input_thread_func, NULL);

    pthread_join(controller_thread, NULL);
    pthread_join(input_thread, NULL);

    timer_delete(timer);
    pthread_mutex_destroy(&shared_data.mutex);

    return 0;
}
