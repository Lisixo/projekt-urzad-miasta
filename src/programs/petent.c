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

void* dziecko_main(void* arg) {
  while(1) {
    //TODO: Dziecko logic
    sleep(1);
  }
  return 0;
}

const char* faculty_to_str(FacultyType t) {
  switch (t) {
    case FACULTY_SC:
      return "Urzad Stanu Cywilnego";
    case FACULTY_KM:
      return "Wydzial Ewidencji Pojazdow i Kierowcow";
    case FACULTY_ML:
      return "Wydzial Mieszkalnictwa";
    case FACULTY_PD:
      return "Wydzial Podatkow i Oplat";
    case FACULTY_SA:
      return "Wydzial Spraw Administracyjnych";
    default:
      return "Do jakiegos wydzialu";
  }
}

int main() {
  // random seed based on getpid() because time(NULL) will be same for all customers
  srand(getpid());

  // attach logger
  int logger_id = msgget(uniq_key(KEY_MAIN_LOGGER), SYNC_PERM);
  if(logger_id == -1) {
    perror("[petent] failed to open logger");
    exit(1);
  }

  // init primary data
  petent_t* petent = malloc(sizeof(petent_t));

  petent->pid = getpid();
  petent->sprawa = (FacultyType)((rand() % 5) + 1);
  petent->numer_biletu = -1;
  
  if(rand() % 200 == 0) {
    petent->dziecko = 1;
    //TODO: safety
    pthread_create(&petent->dziecko, NULL, dziecko_main, NULL);
  } else {
    petent->dziecko = 0;
  }
  
  // attach stan_urzedu
  stan_urzedu_t *urzad;
  {
    int stan_urzedu_shmid = shmget(uniq_key(KEY_SHM_URZAD), sizeof(stan_urzedu_t), SYNC_PERM);
    if(stan_urzedu_shmid == -1) {
      logger_log(logger_id, "[petent] Nie mozna otworzyc stan_urzedu", LOG_ERROR);
      free(petent);
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
      free(petent);
      exit(1);
    }
    petentl = shmat(petent_limit_shmid, NULL, 0);
  }
  
  // wait for urzad setup
  while(urzad->is_operating != 1){
    sleep(5);
  }
  
  // create waiting time
  time_t t = (urzad->time_close - urzad->time_open);
  time_t tx = time(NULL) + (rand() % t);
  struct tm tm = *localtime(&tx);
  
  {
    char txt[256];
    snprintf(txt, sizeof(txt), "[petent][PID:%d] Dzisiaj musze zalatwic sprawe w %s. Przyjde do urzedu okolo %02d:%02d", petent->pid, faculty_to_str(petent->sprawa), tm.tm_hour, tm.tm_min);
    logger_log(logger_id, txt, LOG_DEBUG);
  }

  // wait for opening and arrival
  while(urzad->is_opened != 1 || urzad->time_open > time(NULL)){
    sleep(5);
  }

  // try go into building
  {
    struct sembuf op = {petentl->lobby, petent->ma_dziecko ? -2 : -1};
    if(semop(petentl->limit_sem, &op, 1) == -1) {
      {
        char txt[128];
        snprintf(txt, sizeof(txt), "[petent][PID:%d] Nie moge zajac miejsca w lobby. Wracam do domu", petent->pid);
        logger_log(logger_id, txt, LOG_DEBUG);
      }
      free(petent);
      return 1;
    }
  }

  // entered the building
  {
    char txt[128];
    snprintf(txt, sizeof(txt), "[petent][PID:%d] Wszedlem do budynku", petent->pid);
    logger_log(logger_id, txt, LOG_DEBUG);
  }

  // petent logic

  sleep(2);

  // leave the building
  {
    struct sembuf op = {petentl->lobby, petent->ma_dziecko ? 2 : 1};
    if(semop(petentl->limit_sem, &op, 1) == -1) {
      {
        char txt[128];
        snprintf(txt, sizeof(txt), "[petent][PID:%d] Nie moge wyjsc z urzedu. Niech ktos mnie wypusci", petent->pid);
        logger_log(logger_id, txt, LOG_WARNING);
      }
      free(petent);
    }
  }

  // closing logic
  if(petent->ma_dziecko == 1){
    pthread_join(petent->dziecko, NULL);
  }

  free(petent);
  return 0;
}