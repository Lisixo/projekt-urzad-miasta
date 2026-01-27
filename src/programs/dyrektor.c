#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/signal.h>
#include "helpers/consts.h"
#include "helpers/sync.h"
#include "helpers/logger.h"

void wygeneruj_stan_urzedu(stan_urzedu_t *urzad);

int main() {
  srand(time(NULL));
  signal(SIGUSR1, SIG_IGN);
  signal(SIGUSR2, SIG_IGN);

  // attach logger
  int logger_id = msgget(uniq_key(KEY_MAIN_LOGGER), SYNC_PERM);
  if(logger_id == -1) {
    perror("[dyrektor/init] failed to open logger");
    exit(1);
  }

  // attach stan_urzedu
  stan_urzedu_t *urzad;
  {
    int stan_urzedu_shmid = shmget(uniq_key(KEY_SHM_URZAD), sizeof(stan_urzedu_t), SYNC_PERM);
    if(stan_urzedu_shmid == -1) {
      logger_log(logger_id, "[dyrektor/init] Nie mozna otworzyc stan_urzedu", LOG_ERROR);
      exit(1);
    }
    urzad = shmat(stan_urzedu_shmid, NULL, 0);
  }

  // Setup urzad rules
  logger_log(logger_id, "[dyrektor/init] Ustalanie regul urzedu", LOG_INFO);
  if(sem_lock(urzad->semlock, urzad->semlock_idx) == -1){
    logger_log(logger_id, "[dyrektor/init] Nie mozna zablokowac stan_urzedu", LOG_ERROR);
    shmdt(urzad);
    exit(1);
  }

  wygeneruj_stan_urzedu(urzad);

  // mark urzad as configured
  sem_setvalue(urzad->semlock, urzad->sems.configured, PETENT_COUNT + 1);

  {
    struct tm to = *localtime(&urzad->time_open);
    struct tm tc = *localtime(&urzad->time_close);

    char txt[128];
    snprintf(txt, sizeof(txt), "[dyrektor] Urzad bedzie otwarty od %02d:%02d do %02d:%02d", to.tm_hour, to.tm_min, tc.tm_hour, tc.tm_min);

    logger_log(logger_id, txt, LOG_INFO);
  }

  if(sem_unlock(urzad->semlock, urzad->semlock_idx) == -1){
    logger_log(logger_id, "[dyrektor/init] Nie mozna odblokowac stan_urzedu", LOG_ERROR);
    exit(1);
  }

  logger_log(logger_id, "[dyrektor] Uruchomiono procedure dyrektor", LOG_INFO);

  // Wait for opening time
  logger_log(logger_id, "[dyrektor] Oczekuje na otwarcie urzedu", LOG_INFO);
  {
    time_t t = urzad->time_open - time(NULL);
    if((int)t > 0){
      sync_sleep((int)t);
    }
  }

  // Open building
  logger_log(logger_id, "[dyrektor] Otwieranie urzedu", LOG_INFO);
  sem_lock(urzad->semlock, urzad->semlock_idx);
  urzad->is_opened = 1;
  sem_unlock(urzad->semlock, urzad->semlock_idx);
  sem_setvalue(urzad->semlock, urzad->sems.opened, 100);
  if(sem_setvalue(urzad->semlock, urzad->sems.building, urzad->building_max) == -1) {
    logger_log(logger_id, "[dyrektor] Nie mozna otworzyc lobby", LOG_ERROR);
    exit(1);
  }
  else {
    logger_log(logger_id, "[dyrektor] Urzad zostal otwarty", LOG_INFO);
  }

  // WORKING TIME
  while(time(NULL) < urzad->time_close){
    sync_sleep(1);

    // Event 1: Zwolnienie losowego pracownika szybciej do domu
    // if(rand() % 2000 == 0) {
    //   sem_lock(urzad->semlock, urzad->semlock_idx);
    //   pid_t p = urzad->urzednicy_pids[p % WORKER_COUNT];
    //   sem_unlock(urzad->semlock, urzad->semlock_idx);

    //   kill(p, SIGUSR1);
    // }

    // // Event 2: Ewakuacja
    // if(rand() % 2000 == 0) {
    //   sem_lock(urzad->semlock, urzad->semlock_idx);
      
    //   for(int i=0; i<PETENT_COUNT; i++) {
    //     kill(urzad->petent_pids[i], SIGUSR2);
    //   }

    //   sem_unlock(urzad->semlock, urzad->semlock_idx);
    //   break;
    // }
  }

  // Set close flag
  sem_setvalue(urzad->semlock, urzad->sems.opened, 0);
  sem_lock(urzad->semlock, urzad->semlock_idx);
  urzad->is_opened = 0;
  sem_unlock(urzad->semlock, urzad->semlock_idx);

  // Close lobby
  {
    union semun {
      int val;
      struct semid_ds *buf;
      unsigned short *array;
    } arg;
    arg.val = 0;
    if(sem_setvalue(urzad->semlock, urzad->sems.building, 0) == -1) {
      logger_log(logger_id, "[dyrektor] Nie mozna zamknac lobby", LOG_ERROR);
      exit(1);
    }
  }

  {
    char txt[256];
    snprintf(txt, sizeof(txt), "[dyrektor] Liczba osob oczekujacych przed budynkiem: %d", sem_wait_value(urzad->semlock, urzad->sems.building));
    logger_log(logger_id, txt, LOG_DEBUG);
  }

  logger_log(logger_id, "[dyrektor] Zakonczono procedure dyrektor", LOG_INFO);
  return 0;
}

void wygeneruj_stan_urzedu(stan_urzedu_t *urzad) {
  time_t now = time(NULL);

  if(urzad->time_open != -1) {    
    // urzad->time_open = now + (((rand() % 5)+1)*60); // open in 1-5 minutes
    urzad->time_open = now + 10; // instant
  }
  if(urzad->time_close != -1) {
    // urzad->time_close = urzad->time_open + (((rand() % 110)+10)*60); // left open for 10-120 minutes
    urzad->time_close = urzad->time_open + 5*60; // always 5 minutes
  }
}