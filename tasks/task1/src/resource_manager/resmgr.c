/*
 *  Менеджер ресурсов (Linux версия, скелет для учебного задания)
 *
 *  В ОСРВ роль менеджера ресурсов выполняет resmgr с функциями connect/I/O.
 *  На Linux аналогичное поведение можно смоделировать сервером на UNIX
 *  domain sockets: accept() соответствует open(), recv() — read(), send() — write().
 *
 *  Этот скелет поднимает сервер по пути сокета и обслуживает клиентов в отдельных
 *  потоках. По умолчанию реализовано простое эхо (возврат присланных данных).
 *  СТУДЕНТУ: расширьте протокол, добавьте состояния, буфер устройства, обработку
 *  команд, права доступа и т.д.
 */

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define EXAMPLE_SOCK_PATH "/tmp/example_resmgr.sock"

static const char *progname = "example";
static int optv = 0;
static int listen_fd = -1;

static void options(int argc, char *argv[]);
static void install_signals(void);
static void on_signal(int signo);
static void *client_thread(void *arg);

int main(int argc, char *argv[])
{
  setvbuf(stdout, NULL, _IOLBF, 0);
  printf("%s: starting...\n", progname);
  options(argc, argv);
  install_signals();

  // Создаём UNIX-сокет и биндимся на путь
  listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listen_fd == -1) {
    perror("socket");
    return EXIT_FAILURE;
  }

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, EXAMPLE_SOCK_PATH, sizeof(addr.sun_path) - 1);

  // Удалим старый сокетный файл, если остался после прошлых запусков
  unlink(EXAMPLE_SOCK_PATH);

  if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    perror("bind");
    close(listen_fd);
    return EXIT_FAILURE;
  }

  if (listen(listen_fd, 8) == -1) {
    perror("listen");
    close(listen_fd);
    unlink(EXAMPLE_SOCK_PATH);
    return EXIT_FAILURE;
  }

  printf("%s: listening on %s\n", progname, EXAMPLE_SOCK_PATH);
  printf("Подключитесь клиентом (например: `nc -U %s`) и отправьте данные.\n", EXAMPLE_SOCK_PATH);

  // Основной цикл accept: аналог io_open
  while (1) {
    int client_fd = accept(listen_fd, NULL, NULL);
    if (client_fd == -1) {
      if (errno == EINTR) continue; // прервано сигналом — пробуем снова
      perror("accept");
      break;
    }

    if (optv) {
      printf("%s: io_open — новое подключение (fd=%d)\n", progname, client_fd);
    }

    pthread_t th;
    // Запускаем поток для клиента; поток сам закроет fd
    if (pthread_create(&th, NULL, client_thread, (void *)(long)client_fd) != 0) {
      perror("pthread_create");
      close(client_fd);
      continue;
    }
    pthread_detach(th);
  }

  if (listen_fd != -1) close(listen_fd);
  unlink(EXAMPLE_SOCK_PATH);
  return EXIT_SUCCESS;
}

// Обработчик клиента: recv() как io_read, send() как io_write (эхо)
static void *client_thread(void *arg)
{
  int fd = (int)(long)arg;
  char buf[1024];

  // СТУДЕНТУ: здесь можно выполнять аутентификацию/инициализацию OCB
  // (контекст операции), вести учёт "позиции файла", симулировать флаги и пр.

  for (;;) {
    ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if (n == 0) {
      if (optv) printf("%s: клиент закрыл соединение (fd=%d)\n", progname, fd);
      break;
    }
    if (n < 0) {
      if (errno == EINTR) continue;
      perror("recv");
      break;
    }

    if (optv) {
      printf("%s: io_read — %zd байт\n", progname, n);
    }

    // Простое эхо. СТУДЕНТУ: заменить на логику записи в "устройство".
    ssize_t sent = 0;
    while (sent < n) {
      ssize_t m = send(fd, buf + sent, (size_t)(n - sent), 0);
      if (m < 0) {
        if (errno == EINTR) continue;
        perror("send");
        break;
      }
      sent += m;
    }

    if (optv) {
      printf("%s: io_write — %zd байт\n", progname, sent);
    }
  }

  close(fd);
  return NULL;
}

static void options(int argc, char *argv[])
{
  int opt;
  optv = 0;
  while ((opt = getopt(argc, argv, "v")) != -1) {
    switch (opt) {
      case 'v':
        optv++;
        break;
    }
  }
}

static void install_signals(void)
{
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_signal;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
}

static void on_signal(int signo)
{
  (void)signo;
  if (listen_fd != -1) close(listen_fd);
  unlink(EXAMPLE_SOCK_PATH);
  fprintf(stderr, "\n%s: завершение по сигналу\n", progname);
  _exit(0);
}