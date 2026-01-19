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

int main(int argc, char* argv[]) {
  srand(getpid());

  if(argc != 2){
    fprintf(stderr, "[rejestracja/argv] Wymaganie argumenty: ./rejestracja TICKET_MACHINE_ID");
    exit(1);
  }
  
  // parsing arguments
  int machine_id, count_diff;
  if(sscanf(argv[1], "%d", &machine_id) != 1) {
    fprintf(stderr, "[rejestracja/argv] Niepoprawne argumenty: ./rejestracja TICKET_MACHINE_ID");
    exit(1);
  }

  // attach logger
  int logger_id = msgget(uniq_key(KEY_MAIN_LOGGER), SYNC_PERM);
  if(logger_id == -1) {
    perror("[rejestracja/init] failed to open logger");
    exit(1);
  }

  // attach stan_urzedu
  stan_urzedu_t *urzad;
  {
    int stan_urzedu_shmid = shmget(uniq_key(KEY_SHM_URZAD), sizeof(stan_urzedu_t), SYNC_PERM);
    if(stan_urzedu_shmid == -1) {
      logger_log(logger_id, "[rejestracja/init] Nie mozna otworzyc stan_urzedu", LOG_ERROR);
      exit(1);
    }
    urzad = shmat(stan_urzedu_shmid, NULL, 0);
  }

  // attach petent_limit
  petent_limit_t *petentl;
  {
    int petent_limit_shmid = shmget(uniq_key(KEY_SHM_PETENT_LIMIT), sizeof(petent_limit_t), SYNC_PERM);
    if(petent_limit_shmid == -1) {
      logger_log(logger_id, "[rejestracja/init] Nie mozna otworzyc petent_limit", LOG_ERROR);
      exit(1);
    }
    petentl = shmat(petent_limit_shmid, NULL, 0);
  }

  // attach rejestracja msg
  int rejestracjamsgid = msgget(uniq_key(KEY_MSG_TICKETS), SYNC_PERM);
  if(rejestracjamsgid == -1) {
    logger_log(logger_id, "[rejestracja/init] Nie mozna otworzyc rejestracja_msg MSG", LOG_ERROR);
    exit(1);
  }

  //setup count_diff for opening and closing machine by queue length
  sem_lock(urzad->semlock, urzad->semlock_idx);
  sem_lock(petentl->semlock, petentl->semlock_idx);
  count_diff = (machine_id-1)*(petentl->lobby_max / 3);
  sem_unlock(petentl->semlock, petentl->semlock_idx);
  sem_unlock(urzad->semlock, urzad->semlock_idx);

  {
    char txt[192];
    snprintf(txt, sizeof(txt), "[rejestracja] Biletomat nr %d jest gotowy do pracy. Bedzie czynny od %d osob", machine_id, count_diff);
    logger_log(logger_id, txt, LOG_INFO);
  }
  
  msg_ticket_t tck;
  struct msqid_ds buf;

  int machine_is_opened = 0;

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

  // main loop
  while(urzad_is_opened) {
    // logika otwierania biletomatu
    sem_lock(urzad->semlock, urzad->semlock_idx);
    urzad_is_opened = urzad->is_opened;
    if(machine_is_opened == 0 && urzad->ticket_queue >= count_diff){
      machine_is_opened = 1;
      {
        char txt[64];
        snprintf(txt, sizeof(txt), "[rejestracja] Biletomat nr %d zostaje otwarty", machine_id);
        logger_log(logger_id, txt, LOG_INFO);
      }
    }
    sem_unlock(urzad->semlock, urzad->semlock_idx);

    // logika obslugi biletomatu
    while(machine_is_opened && urzad_is_opened) {
      int tqueue = 0;
      // logika zamykania biletomatu
      sem_lock(urzad->semlock, urzad->semlock_idx);
      urzad_is_opened = urzad->is_opened;
      tqueue = urzad->ticket_queue;
      sem_unlock(urzad->semlock, urzad->semlock_idx);

      if(machine_is_opened == 1 && tqueue < count_diff){
        machine_is_opened = 0;
        {
          char txt[64];
          snprintf(txt, sizeof(txt), "[rejestracja] Biletomat nr %d zostaje zamkniety", machine_id);
          logger_log(logger_id, txt, LOG_INFO);
        }
        break;
      }

      // logika druku biletu
      if(msgrcv(rejestracjamsgid, &tck, sizeof(tck) - sizeof(tck.mtype), 1, IPC_NOWAIT) != -1) {
        sem_lock(petentl->semlock, petentl->semlock_idx);

        int status = 0;
        struct sembuf op;
        op.sem_flg = IPC_NOWAIT;
        op.sem_op = -1;
        switch(tck.facultytype) {
          case FACULTY_KM:
            op.sem_num = petentl->km;
            status = semop(petentl->limit_sem, &op, 1);
            break;
          case FACULTY_ML:
            op.sem_num = petentl->ml;
            status = semop(petentl->limit_sem, &op, 1);
            break;
          case FACULTY_PD:
            op.sem_num = petentl->pd;
            status = semop(petentl->limit_sem, &op, 1);
            break;
          case FACULTY_SC:
            op.sem_num = petentl->sc;
            status = semop(petentl->limit_sem, &op, 1);
            break;
          case FACULTY_SA:
            int s = rand() % 2;
            if(s == 1) {
              op.sem_num = petentl->sa1;
              status = semop(petentl->limit_sem, &op, 1);
              if(status == -1) {
                op.sem_num = petentl->sa2;
                status = semop(petentl->limit_sem, &op, 1);
              }
            }
            else {
              op.sem_num = petentl->sa2;
              status = semop(petentl->limit_sem, &op, 1);
              if(status == -1) {
                op.sem_num = petentl->sa1;
                status = semop(petentl->limit_sem, &op, 1);
              }
            }
            break;
        }

        {
          tck.ticketid = status != -1 ? ++petentl->ticket_count : -1;
          tck.mtype = tck.requester;

          if(msgsnd(rejestracjamsgid, &tck, sizeof(tck) - sizeof(tck.mtype), 0) == -1) {
            logger_log(logger_id, "[rejestracja] Nie mozna wyslac biletu do petenta", LOG_WARNING);
          };
        }

        sem_unlock(petentl->semlock, petentl->semlock_idx);
      }

      sleep(1);
    }
    
    sleep(1);
  }

  return 0;
}