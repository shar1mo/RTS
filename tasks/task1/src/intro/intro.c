#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <sys/types.h>

void *sense(void* arg); 
void *stateOutput(void* arg); 
void *userInterface(void* arg); 
short isRealState(char s); 

char state; 
short changed; 
pthread_cond_t stateCond;
pthread_mutex_t stateMutex; 

#define TRUE 1 
#define FALSE 0 

int FLAG_EXIT = 0;

int main(int argc, char *argv[]) { 
    printf("Hello World!\n"); 

    // Инициализация переменных
    state = 'N'; 
    pthread_cond_init(&stateCond, NULL);
    pthread_mutex_init(&stateMutex, NULL); 

    // Идентификаторы потоков
    pthread_t sensorThread; 
    pthread_t stateOutputThread; 
    pthread_t userThread; 

    // Запуск потоков
    pthread_create(&sensorThread, NULL, sense, NULL); 
    pthread_create(&stateOutputThread, NULL, stateOutput, NULL); 
    pthread_create(&userThread, NULL, userInterface, NULL); 

    // Ожидание завершения потоков
    pthread_join(sensorThread, NULL);
    pthread_join(stateOutputThread, NULL);
    pthread_join(userThread, NULL);

    pthread_mutex_destroy(&stateMutex);
    pthread_cond_destroy(&stateCond);

    printf("Exit Success!\n"); 

    return EXIT_SUCCESS; 
} 

void *sense(void* arg) { 
    char prevState = ' '; 
    while (TRUE) { 
        char tempState; 
        scanf(" %c", &tempState); 
        usleep(10 * 1000);

        pthread_mutex_lock(&stateMutex); 

        if (FLAG_EXIT) {
            pthread_mutex_unlock(&stateMutex);
            break;
        }

        if (isRealState(tempState)) {
            state = tempState;
        }

        if (prevState != state && prevState != (state ^ ' ')) { 
            changed = TRUE; 
            pthread_cond_signal(&stateCond); 
        } 

        prevState = state; 
        pthread_mutex_unlock(&stateMutex); 
    } 
    return NULL; 
} 

short isRealState(char s) { 
    short real = FALSE; 

    if (s == 'R' || s == 'r') //Ready 
        real = TRUE; 
    else if (s == 'N' || s == 'n') //Not Ready 
        real = TRUE; 
    else if (s == 'D' || s == 'd') //Run Mode 
        real = TRUE;
    else if (s == 'E' || s == 'e') //Exit
        real = TRUE;

    return real; 
} 

void *stateOutput(void* arg) { 
    changed = FALSE; 
    while (TRUE) { 
        pthread_mutex_lock(&stateMutex); 

        while (!changed && !FLAG_EXIT) { 
            pthread_cond_wait(&stateCond, &stateMutex); 
        }

        if (FLAG_EXIT) {
            pthread_mutex_unlock(&stateMutex);
            break;
        }
 
        // Вывод нового состояния
        printf("The state has changed! It is now in "); 
        if (state == 'n' || state == 'N') //Not ready 
            printf("Not Ready State\n"); 
        else if (state == 'r' || state == 'R') //Ready 
            printf("Ready State\n"); 
        else if (state == 'd' || state == 'D') //Run Mode 
            printf("Run Mode\n"); 
        else if (state == 'e' || state == 'E') {
            FLAG_EXIT = TRUE;
            printf("Exit\n");
            changed = FALSE;
            pthread_mutex_unlock(&stateMutex);
            break;
        }

        changed = FALSE; 
        pthread_mutex_unlock(&stateMutex); 
    } 
    return NULL; 
} 

void *userInterface(void* arg) { 
    while (TRUE) {
        pthread_mutex_lock(&stateMutex);
        if(FLAG_EXIT) {
            pthread_mutex_unlock(&stateMutex);
            break;
        }

        if (state == 'n' || state == 'N') //Not ready 
            printf("___________________________________________________\n"); 
        else if (state == 'r' || state == 'R') //Ready 
            printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"); 
        else if (state == 'd' || state == 'D') //Run Mode 
            printf("\\_/^\\_/^\\_/^\\_/^\\_/^\\_/^\\_/^\\_/^\\_/^\\_/^\\_/^\\_/^\\_/\n"); 

        pthread_mutex_unlock(&stateMutex);
        usleep(1000 * 1000);
    } 
    return NULL; 
}