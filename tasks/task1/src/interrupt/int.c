#define _POSIX_C_SOURCE 199309L
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

static volatile sig_atomic_t counter = 0;
static volatile sig_atomic_t messages_printed = 0;

static void on_alarm(int signo) {
  (void)signo;
  if (++counter == 100) {
    counter = 0;
    messages_printed++;
    write(STDOUT_FILENO, "100 events\n", 11);
  }
}

int main(void) {
  struct sigaction sa;
  
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_alarm;
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGALRM, &sa, NULL) == -1) {
    perror("sigaction");
    return EXIT_FAILURE;
  }

  struct itimerval itv;
  itv.it_interval.tv_sec = 0;
  itv.it_interval.tv_usec = 10000;
  itv.it_value = itv.it_interval;
  if (setitimer(ITIMER_REAL, &itv, NULL) == -1) {
    perror("setitimer");
    return EXIT_FAILURE;
  }

  printf("timer started. waiting for 10 messages...\n");
  
  while (messages_printed < 10) {
    pause();
  }

  memset(&itv, 0, sizeof(itv));
  setitimer(ITIMER_REAL, &itv, NULL);
  
  printf("done. received 10 messages.\n");
  return EXIT_SUCCESS;
}