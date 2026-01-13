#include "logger.h"
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <sys/msg.h>
#include <time.h>
#include "sync.h"
#include <errno.h>
#include <sys/stat.h>

void* logger_loop(void* arg) {
  Logger* l = (Logger*)arg;
  log_msg_t buf;

  while(l->running) {
    if(msgrcv(l->msgid, &buf, sizeof(buf.mtext), 0, 0) == -1) {
      continue;
    }

    if(buf.mtype < l->level){
      continue;
    }

    fprintf(l->file, "%s\n", buf.mtext);
    fflush(l->file);
  }

  return NULL;
}

const char* level_to_str(LogLevel level) {
  switch (level) {
    case LOG_DEBUG:
      return "DEBUG";
    case LOG_INFO:
      return "INFO";
    case LOG_WARNING:
      return "WARN";
    case LOG_ERROR:
      return "ERROR";
    default:
      return "UNKNOWN";
  }
}

int logger_log(key_t msgid, const char* message, LogLevel level);

Logger* logger_create(key_t id, const char* path, LogLevel level) {
  Logger* logger = malloc(sizeof(Logger));

  logger->msgid = sync_msg_create(id);

  if(logger->msgid == -1) {
    perror("logger_create: failed to create message queue ::");
    sync_msg_destroy(logger->msgid);
    free(logger);
    return NULL;
  } 

  logger->file = fopen(path, "a");

  if(!logger->file) {
    perror("logger_create: failed to open file ::");
    sync_msg_destroy(logger->msgid);
    free(logger);
    return NULL;
  }

  logger->key = id;
  logger->level = level;
  logger->running = 1;

  if(pthread_create(&logger->thread, NULL, logger_loop, logger) != 0) {
    perror("logger_create: failed to create THREAD ::");

    fclose(logger->file);
    sync_msg_destroy(logger->msgid);
    free(logger);
    return NULL;
  }

  logger_log(logger->msgid, "logger thread started", LOG_DEBUG);

  return logger;
}

int logger_log(int msgid, const char* message, LogLevel level) {
  log_msg_t m;
  char prefix[32];
  char tm[20];
  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  m.mtype = level;

  if(strlen(message) > LOG_MSG_SIZE - sizeof(prefix)){
    printf("logger_log: message is too big. it will be truncated ");
  }

  strftime(tm, sizeof(tm), "%Y-%m-%d %H:%M:%S", t);
  snprintf(prefix, sizeof(prefix), "[%s][%s]", level_to_str(level), tm);

  snprintf(m.mtext, sizeof(m.mtext), "%s %s", prefix, message);

  if(msgsnd(msgid, &m, sizeof(m.mtext), 0) == -1) {
    perror("logger_log: failed to send message ::");
    return -1;
  }

  if(level == LOG_ERROR && errno != 0) {
    fprintf(stderr, "%s (errnomsg: %s) \n", m.mtext, strerror(errno));
  }
  else {
    printf("%s\n", m.mtext);
  }

  return 0;
}

int logger_log_key(key_t id, const char* message, LogLevel level) {
  log_msg_t m;
  int msgid = msgget(id, SYNC_PERM);

  if(msgid == -1){
    perror("logger_log: failed to open message queue ::");
    return -1;
  }

  return logger_log(msgid, message, level);
}

void logger_destroy(Logger* logger) {
  if (!logger)
    return;
    
  logger->running = 0;
  logger_log(logger->msgid, "logger thread stoped", LOG_DEBUG);
    
  pthread_join(logger->thread, NULL);
  sync_msg_destroy(logger->msgid);
  fclose(logger->file);
  free(logger);
}