/*
 * Producer (Производитель) для Shared Memory IPC
 *
 * 1. Создает или открывает сегмент разделяемой памяти.
 * 2. Создает или открывает два именованных семафора для синхронизации:
 *    - один показывает, сколько свободного места есть в буфере (для producer'а).
 *    - другой показывает, сколько элементов готовы для чтения (для consumer'а).
 * 3. В цикле записывает данные в кольцевой буфер.
 * 
 *  * Поведение:
 *  - Создаёт/открывает POSIX shared memory.
 *  - Создаёт/открывает именованные семафоры:
 *      SEM_PRODUCER — количество свободных слотов (инициализировано BUFFER_SIZE)
 *      SEM_CONSUMER — количество заполненных слотов (инициализировано 0)
 *  - В цикле записывает увеличивающийся счётчик в слоты кольцевого буфера,
 *    ожидая sem_wait(sem_prod) перед записью и делая sem_post(sem_cons) после записи.
 * 
 *  *  - Без семафоров: producer может перезаписать слот, который consumer ещё не прочитал;
 *    consumer может прочитать "половину" свежезаписанного значения — артефакты:
 *      * пропуски значений (producer перезаписал слот раньше, чем consumer успел прочитать);
 *      * рваные чтения (частично обновлённое значение, если запись крупной структуры не атомарна).
 *  - С семафорами: producer и consumer синхронизированы по количеству свободных/занятых слотов,
 *    порядок и корректность передачи гарантированы (при условии корректной инициализации).
 * 
 * 
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include "shm_common.h"

volatile sig_atomic_t done = 0;
void term(int signum) {
    done = 1;
}

int main() {
    struct sigaction action;
    action.sa_handler = term;
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);

    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        exit(EXIT_FAILURE);
    }
    if (ftruncate(shm_fd, sizeof(shared_data_t)) == -1) {
        perror("ftruncate");
        exit(EXIT_FAILURE);
    }
    shared_data_t *shared_data = mmap(0, sizeof(shared_data_t), PROT_WRITE | PROT_READ, MAP_SHARED, shm_fd, 0);
    if (shared_data == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }
    printf("Producer: Shared memory segment created and mapped.\n");

    sem_t *sem_prod = sem_open(SEM_PRODUCER, O_CREAT, 0666, BUFFER_SIZE);
    if (sem_prod == SEM_FAILED) {
        perror("sem_open producer");
        exit(EXIT_FAILURE);
    }
    sem_t *sem_cons = sem_open(SEM_CONSUMER, O_CREAT, 0666, 0);
    if (sem_cons == SEM_FAILED) {
        perror("sem_open consumer");
        exit(EXIT_FAILURE);
    }
    printf("Producer: Semaphores created.\n");

    shared_data->head = 0;
    shared_data->tail = 0;

    uint64_t counter = 0;
    while (!done) {
        sem_wait(sem_prod);

        shared_data->buffer[shared_data->head] = counter;
        printf("Produced: %llu at index %d\n", (unsigned long long)counter, shared_data->head);
        shared_data->head = (shared_data->head + 1) % BUFFER_SIZE;
        counter++;

        sem_post(sem_cons);

        usleep(100000); 
    }

    printf("\nProducer: End of work...\n");

    munmap(shared_data, sizeof(shared_data_t));
    close(shm_fd);
    shm_unlink(SHM_NAME);

    sem_close(sem_prod);
    sem_close(sem_cons);
    sem_unlink(SEM_PRODUCER);
    sem_unlink(SEM_CONSUMER);

    printf("Producer: Resources freed.\n");
    return 0;
}
