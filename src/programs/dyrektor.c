#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include "helpers/consts.h"
#include "helpers/sync.h"
#include "helpers/logger.h"

void wygeneruj_stan_urzedu(stan_urzedu_t *urzad);

int main() {
  srand(time(NULL));

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

  // attach petent_limit
  petent_limit_t *petentl;
  {
    int petent_limit_shmid = shmget(uniq_key(KEY_SHM_PETENT_LIMIT), sizeof(petent_limit_t), SYNC_PERM);
    if(petent_limit_shmid == -1) {
      logger_log(logger_id, "[dyrektor/init] Nie mozna otworzyc petent_limit", LOG_ERROR);
      exit(1);
    }
    petentl = shmat(petent_limit_shmid, NULL, 0);
  }

  // Setup urzad rules
  logger_log(logger_id, "[dyrektor/init] Ustalanie regul urzedu", LOG_INFO);
  if(sem_lock(urzad->semlock, urzad->semlock_idx) == -1){
    logger_log(logger_id, "[dyrektor/init] Nie mozna zablokowac stan_urzedu", LOG_ERROR);
    shmdt(urzad);
    exit(1);
  }

  wygeneruj_stan_urzedu(urzad);
  urzad->is_operating = 1; // mark stan_urzedu as configured

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
  while(time(NULL) < urzad->time_open){
    sleep(10);
  }

  // Set open flag
  sem_lock(urzad->semlock, urzad->semlock_idx);
  urzad->is_opened = 1;
  sem_unlock(urzad->semlock, urzad->semlock_idx);
  
  // Open lobby
  {
    union semun {
      int val;
      struct semid_ds *buf;
      unsigned short *array;
    } arg;
    arg.val = petentl->lobby_max;
    if(semctl(petentl->limit_sem, petentl->lobby, SETVAL, arg) == -1) {
      logger_log(logger_id, "[dyrektor] Nie mozna otworzyc lobby", LOG_ERROR);
      exit(1);
    }
  }

  // Working time
  logger_log(logger_id, "[dyrektor] Urzad zostal otwarty", LOG_INFO);
  while(time(NULL) < urzad->time_close){
    sleep(1);
  }

  logger_log(logger_id, "[dyrektor] Zakonczono procedure dyrektor", LOG_INFO);
  return 0;
}

void wygeneruj_stan_urzedu(stan_urzedu_t *urzad) {
  time_t now = time(NULL);

  if(urzad->time_open != -1) {    
    urzad->time_open = now + (((rand() % 5)+1)*60); // open in 1-5 minutes
  }
  if(urzad->time_close != -1) {
    urzad->time_close = urzad->time_open + (((rand() % 110)+10)*60); // left open for 10-120 minutes
  }
}