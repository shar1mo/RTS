//  Демонстрация обработки "прерываний" на Linux: сигналы и ввод с клавиатуры.
#define _POSIX_C_SOURCE 199309L
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static const char *progname = "intsimple";

// Флаги сигналов (устанавливаются в обработчиках, читаются в основном цикле)
static volatile sig_atomic_t got_sigint = 0;
static volatile sig_atomic_t got_sigterm = 0;
static volatile sig_atomic_t got_sigusr1 = 0;
static volatile sig_atomic_t got_sigusr2 = 0;
// Для демонстрации: SIGKILL невозможно обработать
static volatile sig_atomic_t got_sigkill = 0; // никогда не будет установлен

static struct termios orig_termios;

static void restore_terminal(void) {
  tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

static int enable_raw_mode(void) {
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) return -1;
  struct termios raw = orig_termios;
  raw.c_lflag &= ~(ICANON | ECHO);
  raw.c_cc[VMIN] = 0;   // неблокирующее чтение
  raw.c_cc[VTIME] = 0;  // без таймаута
  if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == -1) return -1;
  atexit(restore_terminal);
  return 0;
}

static void handle_sigint(int signo)  { (void)signo; got_sigint = 1; }
static void handle_sigterm(int signo) { (void)signo; got_sigterm = 1; }
static void handle_sigusr1(int signo) { (void)signo; got_sigusr1 = 1; }
static void handle_sigusr2(int signo) { (void)signo; got_sigusr2 = 1; }

// обработчик SIGKILL — добавить нельзя, но оставим комментарий
// static void handle_sigkill(int signo) { got_sigkill = 1; } // не работает

int main(void) {
  setvbuf(stdout, NULL, _IOLBF, 0);
  printf("%s: starting...\n", progname);
  printf("Поддерживаемые сигналы: SIGINT(Ctrl+C), SIGTERM, SIGUSR1, SIGUSR2.\n");
  printf("SIGKILL невозможно перехватить, обработать или проигнорировать на Linux.\n");
  printf("Нажмите 'q' для выхода.\n");

  if (enable_raw_mode() == -1) {
    perror("termios");
    return EXIT_FAILURE;
  }

  struct sigaction sa = {0};
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sa.sa_handler = handle_sigint;  sigaction(SIGINT,  &sa, NULL);
  sa.sa_handler = handle_sigterm; sigaction(SIGTERM, &sa, NULL);
  sa.sa_handler = handle_sigusr1; sigaction(SIGUSR1, &sa, NULL);
  sa.sa_handler = handle_sigusr2; sigaction(SIGUSR2, &sa, NULL);
  // SIGKILL установить нельзя — вызовет ошибку:
  // sigaction(SIGKILL, &sa, NULL); // нельзя

  // Основной цикл
  for (;;) {
    // Проверка сигналов
    if (got_sigint)  { got_sigint = 0;  printf("%s: получен SIGINT (Ctrl+C)\n", progname); }
    if (got_sigterm) { got_sigterm = 0; printf("%s: получен SIGTERM\n", progname); }
    if (got_sigusr1) { got_sigusr1 = 0; printf("%s: получен SIGUSR1\n", progname); }
    if (got_sigusr2) { got_sigusr2 = 0; printf("%s: получен SIGUSR2\n", progname); }

    // Неблокирующее чтение клавиатуры
    char ch;
    ssize_t n = read(STDIN_FILENO, &ch, 1);
    if (n == 1) {
      if (ch == 'q' || ch == 'Q') {
        printf("%s: выход по клавише 'q'\n", progname);
        break;
      }
      if (ch == '\n' || ch == '\r') {
        // игнорируем переводы строк
      } else {
        printf("%s: клавиша '%c'\n", progname, ch);
      }
    } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
      perror("read");
      break;
    }

    usleep(10 * 1000);
  }

  printf("%s: exiting...\n", progname);
  return EXIT_SUCCESS;
}
