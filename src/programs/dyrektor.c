#include <stdio.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include "helpers/consts.h"
#include "helpers/sync.h"
#include "helpers/logger.h"

void wygeneruj_stan_urzedu(stan_urzedu_t *urzad);
void wygeneruj_limit_petentow(petent_limit_t *limit);

int main() {
  srand(time(NULL));

  // attach logger
  int logger_id = msgget(uniq_key(KEY_MAIN_LOGGER), SYNC_PERM);
  if(logger_id == -1) {
    perror("[dyrektor] failed to open logger");
    exit(1);
  }

  // attach stan_urzedu
  stan_urzedu_t *urzad;
  {
    int stan_urzedu_shmid = shmget(uniq_key(KEY_SHM_URZAD), sizeof(stan_urzedu_t), SYNC_PERM);
    if(stan_urzedu_shmid == -1) {
      logger_log(logger_id, "[dyrektor] Nie mozna otworzyc stan_urzedu", LOG_ERROR);
      exit(1);
    }
    urzad = shmat(stan_urzedu_shmid, NULL, 0);
  }

  logger_log(logger_id, "[dyrektor] Ustalanie regul urzedu", LOG_INFO);
  sem_lock(urzad->semlock, urzad->semlock_idx);

  wygeneruj_stan_urzedu(urzad);

  {
    char txt[64];
    snprintf(txt, sizeof(txt), "[dyrektor] Urzad bedzie otwarty od %d:%d do %d:%d", urzad->time_open / (60 * 60), (urzad->time_open % 60*60) / 60, urzad->time_close / (60 * 60), (urzad->time_close % 60*60) / 60);

    logger_log(logger_id, txt, LOG_INFO);
  }
  sem_unlock(urzad->semlock, urzad->semlock_idx);

  logger_log(logger_id, "[dyrektor] Uruchomiono procedure dyrektor", LOG_INFO);

  time_t now = time(NULL);
  int daytt = now % (24 * 60 * 60);

  // Wait for opening time
  logger_log(logger_id, "[dyrektor] Oczekuje na otwarcie urzedu", LOG_INFO);
  while(time(NULL) % (24 * 60 * 60) < urzad->time_open){
    sleep(10);
  }

  // Set open flag
  sem_lock(urzad->semlock, urzad->semlock_idx);
  urzad->is_opened = 1;
  sem_unlock(urzad->semlock, urzad->semlock_idx);

  // Working
  logger_log(logger_id, "[dyrektor] Urzad zostal otwarty", LOG_INFO);
  while(time(NULL) % (24 * 60 * 60) < urzad->time_close){
    sleep(1);
  }

  logger_log(logger_id, "[dyrektor] Zakonczono procedure dyrektor", LOG_INFO);
  return 0;
}

void wygeneruj_stan_urzedu(stan_urzedu_t *urzad) {
  time_t now = time(NULL);
  int daytt = now % (24 * 60 * 60);

  urzad->lobby_size = DYREKTOR_MAX_LOBBY;
  if(urzad->time_open != -1) {
    urzad->time_open = daytt + ((rand() % 10)*60+1); // open in 1-10 minutes
  }
  if(urzad->time_close != -1) {
    urzad->time_close = urzad->time_open + ((rand() % 111)*60+10); // left open for 10-120 minutes
  }
}