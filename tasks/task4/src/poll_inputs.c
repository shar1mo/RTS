/*
 * poll_inputs.c
 *
 * Задание 3: Мониторинг нескольких устройств ввода с помощью poll()
 *
 * Программа принимает несколько путей к устройствам ввода (например /dev/input/event2 /dev/input/event4)
 * и отслеживает события на всех них одновременно.
 *
 * Используется системный вызов poll() для одновременного ожидания событий.
 * Для каждого устройства также выводится его имя, полученное через ioctl(EVIOCGNAME).
 *
 * Компиляция:
 *     gcc poll_inputs.c -o poll_inputs
 *
 * Запуск (требуются права root):
 *     sudo ./poll_inputs /dev/input/event2 /dev/input/event4
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>

#define MAX_DEVICES 16

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s /dev/input/eventX1 /dev/input/eventX2 ...\n", argv[0]);
        return 1;
    }

    if (argc - 1 > MAX_DEVICES) {
        fprintf(stderr, "Error: Too many devices. Max is %d.\n", MAX_DEVICES);
        return 1;
    }

    int num_devices = argc - 1;
    struct pollfd fds[MAX_DEVICES];
    char device_names[MAX_DEVICES][256];

    // --- Открываем все устройства и получаем их имена ---
    for (int i = 0; i < num_devices; i++) {
        const char *path = argv[i + 1];
        int fd = open(path, O_RDONLY | O_NONBLOCK); // неблокирующий режим нужен для poll()
        if (fd < 0) {
            fprintf(stderr, "Failed to open %s: %s\n", path, strerror(errno));
            return 1;
        }

        fds[i].fd = fd;
        fds[i].events = POLLIN;

        // Получаем имя устройства
        if (ioctl(fd, EVIOCGNAME(sizeof(device_names[i])), device_names[i]) < 0) {
            snprintf(device_names[i], sizeof(device_names[i]), "Unknown device (%s)", path);
        }

        printf("Opened: %s — %s\n", path, device_names[i]);
    }

    printf("\nMonitoring %d devices. Press Ctrl+C to exit.\n\n", num_devices);

    while (1) {
        int ret = poll(fds, num_devices, -1); // ждать бесконечно
        if (ret < 0) {
            perror("poll failed");
            break;
        }

        for (int i = 0; i < num_devices; i++) {
            if (fds[i].revents & POLLIN) {
                struct input_event ev;
                ssize_t bytes = read(fds[i].fd, &ev, sizeof(ev));

                if (bytes == sizeof(ev)) {
                    // Можно фильтровать только события клавиш, если нужно
                    if (ev.type == EV_KEY) {
                        printf("[%s] type=%d code=%d value=%d\n",
                               device_names[i], ev.type, ev.code, ev.value);
                    }
                } else if (bytes < 0 && errno != EAGAIN) {
                    perror("read failed");
                }
            }
        }
    }

    // --- Очистка ресурсов ---
    for (int i = 0; i < num_devices; i++) {
        close(fds[i].fd);
    }

    return 0;
}
