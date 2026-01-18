#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/ipc.h>

#define LOG_MSG_SIZE 256

typedef enum {
  LOG_DEBUG = 2,
  LOG_INFO,
  LOG_WARNING,
  LOG_ERROR
} LogLevel;

typedef struct log_msg {
  long mtype;
  char mtext[LOG_MSG_SIZE];
} log_msg_t;

typedef struct {
  int msgid;
  FILE* latestfile;
  FILE* historyfile;
  pthread_t thread;
  int running;
  LogLevel level;
  key_t key;
} Logger;

/**
 * Tworzy nowy logger
 * @param latestfile identyfikator kolejki
 * @param latestfile wskaznik do pliku
 * @param level poziom logowania
 * @return id kolejki wiadomosci
 */
Logger* logger_create(key_t id, const char* path, LogLevel level);

int logger_log(int msgid, const char* message, LogLevel level);
int logger_log_key(key_t msgid, const char* message, LogLevel level);

void logger_destroy(Logger* logger);