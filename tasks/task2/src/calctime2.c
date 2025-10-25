#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BILLION 1000000000LL
#define MILLION 1000000LL
#define NUM_SAMPLES 5000  /* 5000 * 2 ms ≈ 10 секунд эксперимента */

/* Вспомогательные функции для конвертации между timespec и наносекундами */
static inline int64_t timespec_to_ns(const struct timespec *ts) {
    return (int64_t)ts->tv_sec * BILLION + (int64_t)ts->tv_nsec;
}

static inline void ns_to_timespec(int64_t ns, struct timespec *ts) {
    ts->tv_sec = (time_t)(ns / BILLION);
    ts->tv_nsec = (long)(ns % BILLION);
}

#ifdef __linux__
int main(void) {
    struct timespec res_rt = {0}, res_mono = {0};
    struct timespec t_next = {0}, now = {0};
    const int64_t period_ns = 2 * MILLION;  /* 2 мс в наносекундах */
    int64_t deltas_ns[NUM_SAMPLES];
    int samples = 0;

    setvbuf(stdout, NULL, _IOLBF, 0);

    /* Проверим разрешение системных часов */
    if (clock_getres(CLOCK_REALTIME, &res_rt) != 0) {
        fprintf(stderr, "clock_getres(CLOCK_REALTIME) failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    if (clock_getres(CLOCK_MONOTONIC, &res_mono) != 0) {
        fprintf(stderr, "clock_getres(CLOCK_MONOTONIC) failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    printf("Resolution: REALTIME=%ld ns, MONOTONIC=%ld ns\n",
           (long)res_rt.tv_nsec, (long)res_mono.tv_nsec);

    /* Начинаем отсчёт от текущего момента CLOCK_MONOTONIC */
    if (clock_gettime(CLOCK_MONOTONIC, &t_next) != 0) {
        fprintf(stderr, "clock_gettime failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    int64_t next_ns = timespec_to_ns(&t_next) + period_ns; /* старт через один период */

    for (samples = 0; samples < NUM_SAMPLES; ++samples) {
        ns_to_timespec(next_ns, &t_next);

        /*
         * Абсолютный сон до момента t_next.
         * TIMER_ABSTIME означает, что clock_nanosleep будет спать
         * "до" указанного абсолютного времени, а не "на" относительный интервал.
         *
         * Это важно: если программа немного задержится (например, из-за планировщика),
         * то при использовании относительного сна ошибка будет накапливаться
         * в каждом цикле ("дрейф таймера").
         * С абсолютным временем ошибка не накапливается — каждое пробуждение
         * синхронизировано по общей временной шкале CLOCK_MONOTONIC.
         */
        int rc;
        do {
            rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t_next, NULL);
        } while (rc == EINTR);  /* повтор при сигнале */
        if (rc != 0) {
            fprintf(stderr, "clock_nanosleep failed: %s\n", strerror(rc));
            return EXIT_FAILURE;
        }

        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
            fprintf(stderr, "clock_gettime failed: %s\n", strerror(errno));
            return EXIT_FAILURE;
        }

        int64_t now_ns = timespec_to_ns(&now);

        /* Фактическая дельта между соседними пробуждениями */
        deltas_ns[samples] = now_ns - (next_ns - period_ns);

        /* Планируем следующее время */
        next_ns += period_ns;
    }

    int64_t min_ns = INT64_MAX, max_ns = INT64_MIN, sum_ns = 0;
    for (int i = 0; i < NUM_SAMPLES; ++i) {
        if (deltas_ns[i] < min_ns) min_ns = deltas_ns[i];
        if (deltas_ns[i] > max_ns) max_ns = deltas_ns[i];
        sum_ns += deltas_ns[i];
    }
    double avg_ns = (double)sum_ns / (double)NUM_SAMPLES;

    double sum_sq_diff = 0;
    for (int i = 0; i < NUM_SAMPLES; ++i) {
        double diff = (double)deltas_ns[i] - avg_ns;
        sum_sq_diff += diff * diff;
    }
    double std_dev_ns = sqrt(sum_sq_diff / NUM_SAMPLES);

    printf("\nPeriod stats over %d samples (target: %" PRId64 " ns):\n",
           NUM_SAMPLES, period_ns);
    printf("  min=%" PRId64 " ns, avg=%.1f ns, max=%" PRId64 " ns, std_dev=%.1f ns\n",
           min_ns, avg_ns, max_ns, std_dev_ns);

    /* Показать первые несколько дельт для иллюстрации */
    printf("\nFirst 10 samples (ns):\n");
    for (int i = 0; i < 10 && i < NUM_SAMPLES; ++i) {
        printf("  sample %d: %" PRId64 "\n", i, deltas_ns[i]);
    }

    return EXIT_SUCCESS;
}
#else
int main(void) {
    struct timespec res_rt = {0};
    const long period_ns = 2 * 1000000L; /* 2 ms */
    struct timespec req;
    struct timespec start, prev, now;
    const int num_samples = 5000;
    long min_ns = 999999999L, max_ns = 0; long long sum_ns = 0;

    setvbuf(stdout, NULL, _IOLBF, 0);
    clock_getres(CLOCK_REALTIME, &res_rt);
    printf("Resolution (CLOCK_REALTIME) ~ %ld ns (emulated periodic sleep)\n", res_rt.tv_nsec);
    clock_gettime(CLOCK_REALTIME, &start);
    prev = start;

    for (int i = 0; i < num_samples; ++i) {
        req.tv_sec = 0; req.tv_nsec = period_ns;
        nanosleep(&req, NULL);
        clock_gettime(CLOCK_REALTIME, &now);
        long delta = (long)((now.tv_sec - prev.tv_sec) * 1000000000LL + (now.tv_nsec - prev.tv_nsec));
        if (delta < min_ns) min_ns = delta;
        if (delta > max_ns) max_ns = delta;
        sum_ns += delta;
        prev = now;
    }
    double avg = (double)sum_ns / (double)num_samples;
    printf("2ms-period stats over %d samples (relative_sleep): min=%ld ns, avg=%.1f ns, max=%ld ns\n",
           num_samples, min_ns, avg, max_ns);
    return EXIT_SUCCESS;
}
#endif