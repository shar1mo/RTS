/*
 * read_input.c
 *
 * Задание 1: Чтение событий с устройства ввода (/dev/input/eventX)
 * Задание 2: Получение имени и физического пути устройства через ioctl()
 *
 * Использование:
 *     sudo ./read_input /dev/input/event3
 *
 * Как найти нужное устройство:
 *     cat /proc/bus/input/devices
 * В строке Handlers= будет указан eventX, например event3.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>
#include <string.h>
#include <sys/ioctl.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s /dev/input/eventX\n", argv[0]);
        return 1;
    }

    const char *device_path = argv[1];

    // --- Проверка прав доступа ---
    if (access(device_path, R_OK) != 0) {
        perror("No read permission for device");
        return 1;
    }

    // --- Открываем файл устройства ---
    int fd = open(device_path, O_RDONLY);
    if (fd < 0) {
        perror("Failed to open device");
        return 1;
    }

    /* --- ЗАДАНИЕ 2: ИСПОЛЬЗОВАНИЕ IOCTL --- */
    char name[256] = "Unknown";
    char phys[256] = "Unknown";

    // Получаем имя устройства
    if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0) {
        perror("ioctl(EVIOCGNAME)");
    } else {
        printf("Device name: %s\n", name);
    }

    // Получаем физический путь устройства (если поддерживается)
    if (ioctl(fd, EVIOCGPHYS(sizeof(phys)), phys) < 0) {
        perror("ioctl(EVIOCGPHYS)");
    } else {
        printf("Physical location: %s\n", phys);
    }

    printf("Reading events from %s. Press Ctrl+C to exit.\n\n", device_path);

    struct input_event ev;
    while (1) {
        ssize_t bytes = read(fd, &ev, sizeof(struct input_event));
        if (bytes != sizeof(struct input_event)) {
            perror("Failed to read event");
            break;
        }

        // Выводим только события клавиатуры (EV_KEY)
        if (ev.type == EV_KEY) {
            printf("Event: type=%d, code=%d, value=%d\n", ev.type, ev.code, ev.value);
            /*
             * value:
             *   1 — клавиша нажата
             *   0 — клавиша отпущена
             *   2 — автоповтор
             */
        }
    }

    close(fd);
    return 0;
}
