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
#include "helpers/utils.h"
#include <errno.h>

#define PRIORITY_OFFSET 10

int main(int argc, char* argv[]) {
  srand(getpid());

  if(argc != 3){
    printf("[urzednik/argv] Wymaganie argumenty: ./urzednik FACULTY SEMLOCK_IDX");
    return 1;
  }
  
  FacultyType type;
  {
    int temp_type;
    if(sscanf(argv[1], "%d", &temp_type) != 1) {
      fprintf(stderr, "[urzednik/argv] Niepoprawny argument FACULTY (required: int)");
      return 1;
    }
    if(temp_type < 1 && temp_type > 6) {
      fprintf(stderr, "[urzednik/argv] Niepoprawny argument FACULTY (required: 1-6 int)");
      return 1;
    }
    type = (FacultyType)temp_type;
  }
  int petentl_semlockidx;
  if(sscanf(argv[2], "%d", &petentl_semlockidx) != 1) {
    fprintf(stderr, "[urzednik/argv] Niepoprawny argument SEMLOCK_IDX (required: int)");
    return 1;
  }

  // attach logger
  int logger_id = msgget(uniq_key(KEY_MAIN_LOGGER), SYNC_PERM);
  if(logger_id == -1) {
    perror("[urzednik/init] failed to open logger");
    exit(1);
  }

  // attach stan_urzedu
  stan_urzedu_t *urzad;
  {
    int stan_urzedu_shmid = shmget(uniq_key(KEY_SHM_URZAD), sizeof(stan_urzedu_t), SYNC_PERM);
    if(stan_urzedu_shmid == -1) {
      logger_log(logger_id, "[urzednik/init] Nie mozna otworzyc stan_urzedu", LOG_ERROR);
      exit(1);
    }
    urzad = shmat(stan_urzedu_shmid, NULL, 0);
  }

  // attach petent_limit
  petent_limit_t *petentl;
  {
    int petent_limit_shmid = shmget(uniq_key(KEY_SHM_PETENT_LIMIT), sizeof(petent_limit_t), SYNC_PERM);
    if(petent_limit_shmid == -1) {
      logger_log(logger_id, "[urzednik/init] Nie mozna otworzyc petent_limit", LOG_ERROR);
      exit(1);
    }
    petentl = shmat(petent_limit_shmid, NULL, 0);
  }

  // attach urzednik msg
  int urzednikmsgid = msgget(uniq_key(KEY_MSG_WORKER), SYNC_PERM);
  if(urzednikmsgid == -1) {
    logger_log(logger_id, "[urzednik/init] Nie mozna otworzyc urzednik_msg MSG", LOG_ERROR);
    exit(1);
  }

  {
    char txt[192];
    snprintf(txt, sizeof(txt), "[urzednik][%s] Czekam na otwarcie urzedu", faculty_to_str(type));
    logger_log(logger_id, txt, LOG_INFO);
  }

  // setup default value for is_opened urzad
  sem_lock(urzad->semlock, urzad->semlock_idx);
  int urzad_is_opened = urzad->is_opened;
  sem_unlock(urzad->semlock, urzad->semlock_idx);

  // wait for urzad to be opened
  while(urzad_is_opened == 0){
    sem_lock(urzad->semlock, urzad->semlock_idx);
    urzad_is_opened = urzad->is_opened;
    sem_unlock(urzad->semlock, urzad->semlock_idx);
    sleep(1);
  }

  {
    char txt[192];
    snprintf(txt, sizeof(txt), "[urzednik][%s] Jestem gotowy do pracy", faculty_to_str(type));
    logger_log(logger_id, txt, LOG_INFO);
  }

  // main loop
  while(urzad_is_opened) {
    // odswiezanie stanu otwarcia urzedu
    sem_lock(urzad->semlock, urzad->semlock_idx);
    urzad_is_opened = urzad->is_opened;
    sem_unlock(urzad->semlock, urzad->semlock_idx);

    // wejscie petenta
    msg_ticket_t tck;
    if(msgrcv(urzednikmsgid, &tck, sizeof(tck) - sizeof(tck.mtype), type + PRIORITY_OFFSET, IPC_NOWAIT) == -1){
      if(errno != ENOMSG) {
        logger_log(logger_id, "Nie mozna pobrac informacji z kolejki urzednicy msg", LOG_ERROR);
        exit(1);
      }
      if(msgrcv(urzednikmsgid, &tck, sizeof(tck) - sizeof(tck.mtype), type, IPC_NOWAIT) == -1) {
        sleep(1);
        continue;
      }
    }

    sleep((rand() % 5) + 5); // symulacja zalatwiania sprawy dla petenta przez 5-10 sekund

    if(type == FACULTY_SA && rand() % 100 < 40) {
      tck.redirected_from = tck.facultytype;

      switch(rand() % 4) {
        case 0:
          tck.mtype = FACULTY_KM;
          break;
        case 1:
          tck.mtype = FACULTY_ML;
          break;
        case 2:
          tck.mtype = FACULTY_PD;
          break;
        case 3:
          tck.mtype = FACULTY_SC;
          break;
      }
    }
    else if(type != FACULTY_SA && type != CASHIER_POINT && tck.payment == PAYMENT_NOT_NEEDED && rand() % 10 == 0){
      tck.payment = PAYMENT_NEEDED;

      tck.redirected_from = tck.facultytype;
      tck.mtype = CASHIER_POINT;
    }
    else if(type == CASHIER_POINT) {
      tck.payment = PAYMENT_SUCCESS;
      tck.facultytype = tck.redirected_from;
      tck.redirected_from = CASHIER_POINT;
      tck.mtype = tck.redirected_from + PRIORITY_OFFSET;
    }
    else {
      tck.facultytype = 0;
    }

    // {
    //   char txt[192];
    //   snprintf(txt, sizeof(txt), "[urzednik][%s] blokada 1", faculty_to_str(type));
    //   logger_log(logger_id, txt, LOG_INFO);
    // }

    sem_lock(petentl->semlock, petentl->semlock_idx);
    tck.ticketid = ++petentl->ticket_count;
    sem_unlock(petentl->semlock, petentl->semlock_idx);

    tck.mtype = tck.requester;
    if(msgsnd(urzednikmsgid, &tck, sizeof(tck) - sizeof(tck.mtype), 0) == -1) {
      logger_log(logger_id, "[urzednik] Nie mozna wyslac biletu do petenta", LOG_WARNING);
    };

    // zmiejszanie puli dziennych petentow lub koniec pracy (tylko gdy ma sie idx semlocka)
    struct sembuf op = {petentl_semlockidx, -1, IPC_NOWAIT};
    if(petentl_semlockidx != 0 && semop(petentl->limit_sem, &op, 1) == -1){
      if(errno == EAGAIN) {
        logger_log(logger_id, "Osiagnalem limit petentow. Koncze na dzisiaj prace", LOG_INFO);

        union semun {
          int val;
          struct semid_ds *buf;
          unsigned short *array;
        } arg;

        // Lock queue and reject all
        arg.val = 0;
        semctl(petentl->limit_sem, petentl_semlockidx, SETVAL, arg);

        while(msgrcv(urzednikmsgid, &tck, sizeof(tck) - sizeof(tck.mtype), petentl_semlockidx, IPC_NOWAIT) != -1){
          tck.mtype = tck.requester;
          tck.ticketid = -1;
          msgsnd(urzednikmsgid, &tck, sizeof(tck) - sizeof(tck.mtype), 0);
        }
        break;
      }
    }
    sleep(1);
  }

  return 0;
}