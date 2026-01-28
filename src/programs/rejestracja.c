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

int main(int argc, char* argv[]) {
  signal(SIGUSR1, SIG_IGN);
  signal(SIGUSR2, SIG_IGN);
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

  // attach global msg
  int global_msg_id = msgget(uniq_key(KEY_MSG_GLOBAL), SYNC_PERM);
  if(global_msg_id == -1) {
    logger_log(logger_id, "[rejestracja/init] Nie mozna otworzyc global_msg MSG", LOG_ERROR);
    exit(1);
  }

  //setup count_diff for opening and closing machine by queue length
  sem_lock(urzad->semlock, urzad->semlock_idx);
  count_diff = (machine_id)*(urzad->building_max / TICKET_MACHINE_COUNT);
  sem_unlock(urzad->semlock, urzad->semlock_idx);

  {
    char txt[192];
    snprintf(txt, sizeof(txt), "[rejestracja] Biletomat nr %d jest gotowy do pracy. Bedzie czynny od %d osob", machine_id, count_diff);
    logger_log(logger_id, txt, LOG_INFO);
  }
  
  msg_ticket_t tck;
  struct msqid_ds buf;

  int machine_is_opened = 0;

  // wait for urzad to be opened
  sem_lock(urzad->semlock, urzad->sems.opened);
  sem_unlock(urzad->semlock, urzad->sems.opened);

  sem_lock(urzad->semlock, urzad->semlock_idx);
  int urzad_is_opened = urzad->is_opened;
  sem_unlock(urzad->semlock, urzad->semlock_idx);

  // main loop
  while(urzad_is_opened) {
    // odczyt wielkosci kolejki
    int tqueue = sem_wait_value(urzad->semlock, urzad->sems.tickets);
    if(tqueue == -1) {
      logger_log(logger_id, "[rejestracja] Nie mozna odczytac liczby osob oczekujacych w kolejce", LOG_ERROR);
      exit(1);
    }
    // odczyt stanu urzedu
    sem_lock(urzad->semlock, urzad->semlock_idx);
    urzad_is_opened = urzad->is_opened;
    sem_unlock(urzad->semlock, urzad->semlock_idx);

    if(urzad_is_opened == 0) {
      break;
    }

    // logika otwierania biletomatu
    if(machine_is_opened == 0 && tqueue >= count_diff){
      machine_is_opened = 1;
      // sem_unlock(urzad->semlock, urzad->sems.tickets);
      {
        char txt[64];
        snprintf(txt, sizeof(txt), "[rejestracja] Biletomat nr %d zostaje otwarty", machine_id);
        logger_log(logger_id, txt, LOG_INFO);
      }
    }

    // logika obslugi biletomatu
    while(machine_is_opened && urzad_is_opened) {
      // odczyt wielkosci kolejki
      tqueue = sem_wait_value(urzad->semlock, urzad->sems.tickets);
      if(tqueue == -1) {
        logger_log(logger_id, "[rejestracja] Nie mozna odczytac liczby osob oczekujacych w kolejce", LOG_ERROR);
        exit(1);
      }
      // odczyt stanu urzedu
      sem_lock(urzad->semlock, urzad->semlock_idx);
      urzad_is_opened = urzad->is_opened;
      sem_unlock(urzad->semlock, urzad->semlock_idx);

      if(urzad_is_opened == 0 || (machine_is_opened == 1 && tqueue < count_diff)){
        machine_is_opened = 0;
        // sem_lock(urzad->semlock, urzad->sems.tickets);
        {
          char txt[64];
          snprintf(txt, sizeof(txt), "[rejestracja] Biletomat nr %d zostaje zamkniety", machine_id);
          logger_log(logger_id, txt, LOG_INFO);
        }
        break;
      }

      // logika druku biletu
      if(msgrcv(global_msg_id, &tck, sizeof(tck) - sizeof(tck.mtype), 1, IPC_NOWAIT) != -1) {
        sync_sleep((rand() % 3) + 2); // symulacja zalatwiania sprawy dla petenta przez 2-5 sekund

        sem_lock(urzad->semlock, urzad->semlock_idx);

        int original_ticket_id = tck.ticketid;
        int original_faculty = tck.facultytype;

        int status = 0;
        struct sembuf op;
        op.sem_flg = IPC_NOWAIT;
        op.sem_op = -1;
        switch(tck.facultytype) {
          case FACULTY_KM:
            op.sem_num = urzad->sems.km_limit;
            status = semop(urzad->semlock, &op, 1);
            break;
          case FACULTY_ML:
            op.sem_num = urzad->sems.ml_limit;
            status = semop(urzad->semlock, &op, 1);
            break;
          case FACULTY_PD:
            op.sem_num = urzad->sems.pd_limit;
            status = semop(urzad->semlock, &op, 1);
            break;
          case FACULTY_SC:
            op.sem_num = urzad->sems.sc_limit;
            status = semop(urzad->semlock, &op, 1);
            break;
          case FACULTY_SA:
            int s = rand() % 2;
            if(s == 1) {
              op.sem_num = urzad->sems.sa1_limit;
              status = semop(urzad->semlock, &op, 1);
              if(status == -1) {
                op.sem_num = urzad->sems.sa2_limit;
                status = semop(urzad->semlock, &op, 1);
              }
            }
            else {
              op.sem_num = urzad->sems.sa2_limit;
              status = semop(urzad->semlock, &op, 1);
              if(status == -1) {
                op.sem_num = urzad->sems.sa1_limit;
                status = semop(urzad->semlock, &op, 1);
              }
            }
            break;
        }

        if(status == -1){
          logger_log(logger_id, "[rejestracja] Limit zostal osiagniety", LOG_WARNING);
        }

        {
          tck.ticketid = status != -1 ? ++urzad->taken_ticket_count : -1;
          tck.mtype = tck.requester;
          tck.queueid = tck.facultytype + (tck.redirected_from == 1 ? D_MSG_WORKER_PRIORITY_OFFSET : D_MSG_WORKER_OFFSET);

          if(msgsnd(global_msg_id, &tck, sizeof(tck) - sizeof(tck.mtype), 0) == -1) {
            logger_log(logger_id, "[rejestracja] Nie mozna wyslac biletu do petenta", LOG_WARNING);
          };
        }

        // Send dyrektor a message if petent can't receive a ticket besacuse limit is exceeded
        if(tck.ticketid == -1){
          tck.mtype = 2;
          tck.ticketid = original_ticket_id;
          tck.facultytype = original_faculty;
          tck.queueid = 1;
          msgsnd(global_msg_id, &tck, sizeof(tck) - sizeof(tck.mtype), 0);
        }

        sem_unlock(urzad->semlock, urzad->semlock_idx);
      }

      // obsluga blokady globalnej wejscia
      sem_lock(urzad->semlock, urzad->semlock_idx);
      int is_locked = urzad->is_locked;
      if(
        (sem_getvalue(urzad->semlock, urzad->sems.km_limit) + 
        sem_getvalue(urzad->semlock, urzad->sems.ml_limit) + 
        sem_getvalue(urzad->semlock, urzad->sems.pd_limit) + 
        sem_getvalue(urzad->semlock, urzad->sems.sc_limit) + 
        sem_getvalue(urzad->semlock, urzad->sems.sa1_limit) + 
        sem_getvalue(urzad->semlock, urzad->sems.sa2_limit) == 0)
        && is_locked == 0
      ){
        logger_log(logger_id, "[rejestracja] Limity zostaly osiagniete. Blokujemy wejscie", LOG_INFO);
        urzad->is_locked = 1;
        sem_setvalue(urzad->semlock, urzad->sems.building, 0);
      }
      sem_unlock(urzad->semlock, urzad->semlock_idx);

      sync_sleep(1);
    }
    
    sync_sleep(1);
  }

  return 0;
}