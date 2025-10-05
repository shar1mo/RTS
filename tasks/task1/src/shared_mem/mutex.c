#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

#define NumThreads      16       // позже установите значение 16

volatile int     var1;
volatile int     var2;

void    *update_thread (void *);
char    *progname = "mutex";

pthread_mutex_t var_mutex;

int main ()
{
    pthread_t           threadID [NumThreads];
    pthread_attr_t      attrib;
    struct sched_param  param;
    int                 i, policy;
    if (pthread_mutex_init(&var_mutex, NULL) != 0) {
        printf("%s: err init mutex\n", progname);
        exit(1);
    }
    setvbuf (stdout, NULL, _IOLBF, 0);
    var1 = var2 = 0;        /* инициализация известных */
    printf ("%s:  starting; creating %d threads\n", progname, NumThreads);

    // pthread_getschedparam (pthread_self(), &policy, &param);
    // pthread_attr_init (&attrib);
    // pthread_attr_setinheritsched (&attrib, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy (&attrib, SCHED_RR);
    param.sched_priority -= 2;        // Снизить приоритет на 2 уровня
    pthread_attr_setschedparam (&attrib, &param);

    for (i = 0; i < NumThreads; i++) {
        long *thread_num = malloc(sizeof(long));
        *thread_num = i;
        pthread_create (&threadID[i], NULL, update_thread, (void *)thread_num);
        printf("%s: created thread %d\n", progname, i);
    }

    printf("%s: all threads created, running for 60 seconds...\n", progname);
    
    for (i = 1; i <= 2; i++) {
        sleep(10);
        pthread_mutex_lock(&var_mutex);
        printf("%s: [%d/20s] var1=%d, var2=%d\n", progname, i*10, var1, var2);
        pthread_mutex_unlock(&var_mutex);
    }
    
    printf ("%s:  stopping; cancelling threads\n", progname);
    for (i = 0; i < NumThreads; i++) {
      pthread_cancel (threadID [i]);
      printf("%s: cancelled thread %d\n", progname, i);
    }

    for (i = 0; i < NumThreads; i++) {
        pthread_join(threadID[i], NULL);
        printf("%s: thread %d joined\n", progname, i);
    }

    printf ("%s:  all done, var1 is %d, var2 is %d\n", progname, var1, var2);
    if (var1 == var2) {
        printf("%s: SUCCESS - variables are synchronized\n", progname);
    } else {
        printf("%s: ERROR - variables are not synchronized\n", progname);
    }
    pthread_mutex_destroy(&var_mutex);
    fflush (stdout);
    exit (0);
}

void do_work()
{
    static int call_count = 0;
    call_count++;

    if (call_count % 500000 == 0) {
        printf ("%s: do_work called %d times total\n", progname, call_count);
    }
}

void *update_thread (void *arg)
{
    long thread_num = *(long*)arg;
    free(arg);
    int iteration = 0;

    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    
    printf("%s: thread %ld started\n", progname, thread_num);
    
    while (1) {
        pthread_mutex_lock(&var_mutex);
        
        if (var1 != var2) {
            printf ("%s: ERROR - thread %ld, var1 (%d) != var2 (%d)!\n",
                    progname, thread_num, var1, var2);
            var1 = var2;
        }
        
        /* do some work here */
        do_work(); 
        var1++;
        var2++;
        iteration++;

        if (iteration % 1000000 == 0) {
            printf("%s: thread %ld iteration %d, var1=%d var2=%d)\n", 
                   progname, thread_num, iteration, var1, var2);
        }

        pthread_mutex_unlock(&var_mutex);
    }
    return (NULL);
}