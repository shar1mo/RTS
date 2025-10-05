/*
 *  Менеджер ресурсов (Linux версия, расширенная реализация)
 *
 *  Моделируется поведение менеджера ресурсов ОСРВ:
 *  - UNIX domain socket как точка подключения
 *  - отдельный поток на клиента
 *  - простейший протокол команд:
 *      WRITE <text>  — записать данные в буфер
 *      READ          — прочитать содержимое буфера
 *      CLEAR         — очистить буфер
 *      STATUS        — узнать длину и состояние буфера
 *      EXIT / QUIT   — закрыть соединение
 *
 *  Один клиент может "захватить" устройство для записи (эксклюзивный доступ).
 *  Остальные смогут только читать.
 */
#define _POSIX_C_SOURCE 199309L
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define EXAMPLE_SOCK_PATH "/tmp/example_resmgr.sock"

static const char *progname = "example";
static int optv = 0;
static int listen_fd = -1;

// >>> добавлено: состояние "устройства"
#define DEVICE_BUFSIZE 4096
static char device_buf[DEVICE_BUFSIZE];
static size_t device_len = 0;
static pthread_mutex_t device_lock = PTHREAD_MUTEX_INITIALIZER;
static int writer_active = 0; // 0 — нет владельца, иначе fd клиента

// объявления
static void options(int argc, char *argv[]);
static void install_signals(void);
static void on_signal(int signo);
static void *client_thread(void *arg);

// ----------------------------------------------------------
int main(int argc, char *argv[])
{
  setvbuf(stdout, NULL, _IOLBF, 0);
  printf("%s: starting...\n", progname);
  options(argc, argv);
  install_signals();

  listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listen_fd == -1) {
    perror("socket");
    return EXIT_FAILURE;
  }

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, EXAMPLE_SOCK_PATH, sizeof(addr.sun_path) - 1);
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
  printf("Подключитесь клиентом (например: `nc -U %s`)\n", EXAMPLE_SOCK_PATH);
  printf("Доступные команды: WRITE <txt>, READ, CLEAR, STATUS, EXIT.\n");

  while (1) {
    int client_fd = accept(listen_fd, NULL, NULL);
    if (client_fd == -1) {
      if (errno == EINTR) continue;
      perror("accept");
      break;
    }

    if (optv)
      printf("%s: io_open — новое подключение (fd=%d)\n", progname, client_fd);

    pthread_t th;
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

// ----------------------------------------------------------
// Клиентский поток: простейший протокол команд
static void *client_thread(void *arg)
{
  int fd = (int)(long)arg;
  char buf[1024];
  ssize_t n;

  while ((n = recv(fd, buf, sizeof(buf) - 1, 0)) > 0) {
    buf[n] = '\0';

    // уберём перевод строки
    char *newline = strchr(buf, '\n');
    if (newline) *newline = '\0';

    if (optv) printf("%s: команда от fd=%d: '%s'\n", progname, fd, buf);

    // команда EXIT / QUIT
    if (strcasecmp(buf, "EXIT") == 0 || strcasecmp(buf, "QUIT") == 0) {
      send(fd, "OK: bye\n", 8, 0);
      break;
    }
    // команда STATUS
    else if (strcasecmp(buf, "STATUS") == 0) {
      pthread_mutex_lock(&device_lock);
      char msg[128];
      snprintf(msg, sizeof(msg),
               "BUF_LEN=%zu, WRITER=%s\n",
               device_len, (writer_active == 0 ? "none" : "active"));
      pthread_mutex_unlock(&device_lock);
      send(fd, msg, strlen(msg), 0);
    }
    // команда READ
    else if (strcasecmp(buf, "READ") == 0) {
      pthread_mutex_lock(&device_lock);
      if (device_len == 0)
        send(fd, "(empty)\n", 8, 0);
      else
        send(fd, device_buf, device_len, 0);
      pthread_mutex_unlock(&device_lock);
    }
    // команда CLEAR
    else if (strcasecmp(buf, "CLEAR") == 0) {
      pthread_mutex_lock(&device_lock);
      device_len = 0;
      device_buf[0] = '\0';
      pthread_mutex_unlock(&device_lock);
      send(fd, "OK: buffer cleared\n", 19, 0);
    }
    // команда WRITE <text>
    else if (strncasecmp(buf, "WRITE ", 6) == 0) {
      const char *data = buf + 6;
      pthread_mutex_lock(&device_lock);

      if (writer_active != 0 && writer_active != fd) {
        pthread_mutex_unlock(&device_lock);
        send(fd, "ERR: device busy\n", 17, 0);
        continue;
      }

      writer_active = fd; // захватываем устройство

      size_t len = strlen(data);
      if (len + device_len >= DEVICE_BUFSIZE) len = DEVICE_BUFSIZE - device_len - 1;
      memcpy(device_buf + device_len, data, len);
      device_len += len;
      device_buf[device_len] = '\0';

      pthread_mutex_unlock(&device_lock);
      send(fd, "OK: written\n", 12, 0);
    }
    else {
      const char *msg = "ERR: unknown command\n";
      send(fd, msg, strlen(msg), 0);
    }
  }

  if (optv)
    printf("%s: клиент отключён (fd=%d)\n", progname, fd);

  pthread_mutex_lock(&device_lock);
  if (writer_active == fd) writer_active = 0;
  pthread_mutex_unlock(&device_lock);

  close(fd);
  return NULL;
}

// ----------------------------------------------------------
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

// ----------------------------------------------------------
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
