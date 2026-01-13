#include <stdio.h>
#include "helpers/logger.h"
#include "helpers/sync.h"
#include "helpers/consts.h"
#include "sys/shm.h"
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

typedef struct {
  Logger* log;
  int semlock_ins;
  int stan_urzedu_shm;
  int petent_limit_shm;
} res_t;

void free_res(res_t* res);

int shutdown(res_t* res) {
  free_res(res);
  exit(1);
}

res_t* alloc_res() {
  res_t* res = malloc(sizeof(res_t));

  // logger init
  res->log = logger_create(uniq_key(KEY_MAIN_LOGGER), "./logs/latest.txt", LOG_DEBUG);
  if(!res->log) {
    printf("alloc_res: Nie mozna utworzyc loggera. Zwalnianie zasobow\n");
    shutdown(res);
  }

  // shm semlock init
  res->semlock_ins = sem_create(uniq_key(KEY_SHM_SEMLOCK), 2, 1);
  int curr_semlock_idx = 0;
  if(res->semlock_ins == -1) {
    logger_log(res->log->msgid, "alloc_res: Nie mozna utworzyc semlock_ins", LOG_ERROR);
    shutdown(res);
  }

  // urzad shm init
  int urzadshmid = shmget(uniq_key(KEY_SHM_URZAD), sizeof(stan_urzedu_t), SYNC_PERM | IPC_CREAT | IPC_EXCL);
  if(urzadshmid == -1){
    logger_log(res->log->msgid, "alloc_res: Nie mozna utworzyc stan_urzedu SHM. Zwalnianie zasobow\n", LOG_ERROR);
    shutdown(res);
  }
  else {
    res->stan_urzedu_shm = urzadshmid;

    stan_urzedu_t* urzad;
    urzad = shmat(res->stan_urzedu_shm, NULL, 0);

    urzad->semlock = res->semlock_ins;
    urzad->semlock_idx = curr_semlock_idx++;

    sem_lock(urzad->semlock, urzad->semlock_idx);

    urzad->is_opened = 0;
    urzad->lobby_size = 0;
    urzad->time_close = 0;
    urzad->time_open = 0;
    urzad->queue_count = 0;
    urzad->current_lobby_size = 0;
    urzad->lobby_size = 0;
    
    sem_unlock(urzad->semlock, urzad->semlock_idx);
    shmdt(urzad);
  }
  

  // petent_limit shm init
  int petentlimitshmid = shmget(uniq_key(KEY_SHM_PETENT_LIMIT), sizeof(petent_limit_t), SYNC_PERM | IPC_CREAT | IPC_EXCL);
  if(petentlimitshmid == -1){
    logger_log(res->log->msgid, "alloc_res: Nie mozna utworzyc petent_limit SHM. Zwalnianie zasobow\n", LOG_ERROR);
    shutdown(res);
  }
  else {
    res->petent_limit_shm = petentlimitshmid;

    petent_limit_t *l;

    l = shmat(res->petent_limit_shm, NULL, 0);

    l->semlock = res->semlock_ins;
    l->semlock_idx = curr_semlock_idx++;

    sem_lock(l->semlock, l->semlock_idx);

    l->km = PETENT_COUNT * 10 / 100;
    l->pd = PETENT_COUNT * 10 / 100;
    l->ml = PETENT_COUNT * 10 / 100;
    l->sc = PETENT_COUNT * 10 / 100;
    l->sa = PETENT_COUNT * 60 / 100 / 2;
    
    sem_unlock(l->semlock, l->semlock_idx);
    shmdt(l);
  }

  logger_log(res->log->msgid, "alloc_res: success", LOG_DEBUG);
  return res;
}

void free_res(res_t* res) {
  if(res->semlock_ins) {
    if(sem_destroy(res->semlock_ins) == -1) {
      logger_log(res->log->msgid, "free_res: Nie mozna usunac semlock_ins", LOG_ERROR);
    }
  }
  if(res->petent_limit_shm) {
    if(shmctl(res->petent_limit_shm, IPC_RMID, NULL) == -1){
      logger_log(res->log->msgid, "free_res: Nie mozna usunac petent_limit shm\n", LOG_ERROR);
    }
  }
  if(res->stan_urzedu_shm) {
    if(shmctl(res->stan_urzedu_shm, IPC_RMID, NULL) == -1){
      logger_log(res->log->msgid, "free_res: Nie mozna usunac stan_urzedu shm\n", LOG_ERROR);
    }
  }
  
  if(res->log) {
    logger_destroy(res->log);
  }
  free(res);
}

// using as global to close when signal received
res_t *res = NULL;

void exit_signal_handler(int sig) {
  char txt[64];
  snprintf(txt, sizeof(txt), "Odebrano kod %d. Zamykanie aplikacji", sig);
  if(res->log){
    logger_log(res->log->msgid, txt, LOG_WARNING);
  } else {
    printf("%s", txt);
  }
  shutdown(res);
}

int main() {
  signal(SIGINT, exit_signal_handler);
  res = alloc_res();

  sleep(1);

  pid_t dyrektor_pid = fork();
  if(dyrektor_pid == 0) {
    execl("./dyrektor", "dyrektor", NULL);

    logger_log(res->log->msgid, "Nie mozna uruchomic dyrektora!", LOG_ERROR);
    shutdown(res);
  } else {
    int status;
    wait(&status);
  }

  free_res(res);
  return 0;
}