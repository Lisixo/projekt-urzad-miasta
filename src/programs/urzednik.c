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
#include "helpers/utils.h"
#include <errno.h>

volatile sig_atomic_t sig1;

void signal1handler(int sig) {
  sig1 = 1;
}

int main(int argc, char* argv[]) {
  srand(getpid());
  signal(SIGUSR1, signal1handler);
  signal(SIGUSR2, SIG_IGN);

  if(argc != 4){
    printf("[urzednik/argv] Wymaganie argumenty: ./urzednik FACULTY QUEUE_SEMLOCK LIMIT_SEMLOCK");
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
  int queue_semlock_idx;
  if(sscanf(argv[2], "%d", &queue_semlock_idx) != 1) {
    fprintf(stderr, "[urzednik/argv] Niepoprawny argument QUEUE_SEMLOCK (required: int)");
    return 1;
  }
  int limit_semlock_idx;
  if(sscanf(argv[3], "%d", &limit_semlock_idx) != 1) {
    fprintf(stderr, "[urzednik/argv] Niepoprawny argument LIMIT_SEMLOCK (required: int)");
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

  // attach urzednik msg
  int global_msg_id = msgget(uniq_key(KEY_MSG_GLOBAL), SYNC_PERM);
  if(global_msg_id == -1) {
    logger_log(logger_id, "[urzednik/init] Nie mozna otworzyc global MSG", LOG_ERROR);
    exit(1);
  }

  {
    char txt[192];
    snprintf(txt, sizeof(txt), "[urzednik][%s] Czekam na otwarcie urzedu", faculty_to_str(type));
    logger_log(logger_id, txt, LOG_INFO);
  }

  // wait for urzad to be opened
  sem_lock(urzad->semlock, urzad->sems.opened);
  sem_unlock(urzad->semlock, urzad->sems.opened);
  int urzad_is_opened = 1;

  // open for clients
  sem_unlock(urzad->semlock, queue_semlock_idx);

  {
    char txt[192];
    snprintf(txt, sizeof(txt), "[urzednik][%s] Jestem gotowy do pracy", faculty_to_str(type));
    logger_log(logger_id, txt, LOG_INFO);
  }

  int should_work = 1;

  // main loop
  while(should_work) {
    // odswiezanie stanu otwarcia urzedu
    sem_lock(urzad->semlock, urzad->semlock_idx);
    urzad_is_opened = urzad->is_opened;
    sem_unlock(urzad->semlock, urzad->semlock_idx);

    switch(urzad_is_opened) {
      case -1:
        logger_log(logger_id, "[urzednik/loop] Nie moge sprawdzic stanu urzedu", LOG_ERROR);
        should_work = 0;
      case 0:
        should_work = 0;
        break;
    }

    // wejscie petenta
    msg_ticket_t tck;
    int vip = 0;
    if(msgrcv(global_msg_id, &tck, sizeof(tck) - sizeof(tck.mtype), type + D_MSG_WORKER_PRIORITY_OFFSET, IPC_NOWAIT) == -1){
      if(errno != ENOMSG) {
        logger_log(logger_id, "Nie mozna pobrac informacji z kolejki urzednicy msg", LOG_ERROR);
        exit(1);
      }
      if(msgrcv(global_msg_id, &tck, sizeof(tck) - sizeof(tck.mtype), type + D_MSG_WORKER_OFFSET, IPC_NOWAIT) == -1) {
        sync_sleep(1);
        continue;
      }
      else {
        vip = 0;
      }
    }
    else {
      vip = 1;
    }

    {
      char txt[128];
      snprintf(txt, sizeof(txt), "[urzednik][%s] Przyjmuje petenta %d", faculty_to_str(type), tck.requester);
      logger_log(logger_id, txt, LOG_INFO);
    }

    sync_sleep((rand() % 5) + 5); // symulacja zalatwiania sprawy dla petenta przez 5-10 sekund

    if(type == FACULTY_SA && rand() % 100 < 40) {
      tck.redirected_from = tck.facultytype;
      int limit_idx;

      int status = 0;
      struct sembuf op;
      op.sem_flg = IPC_NOWAIT;
      op.sem_op = -1;

      switch(rand() % 4) {
        case 0:
          op.sem_num = urzad->sems.km_limit;
          tck.facultytype = FACULTY_KM;
          break;
        case 1:
          op.sem_num = urzad->sems.ml_limit;
          tck.facultytype = FACULTY_ML;
          break;
        case 2:
          op.sem_num = urzad->sems.pd_limit;
          tck.facultytype = FACULTY_PD;
          break;
        case 3:
          op.sem_num = urzad->sems.sc_limit;
          tck.facultytype = FACULTY_SC;
          break;
      }
      
      if(semop(urzad->semlock, &op, 1) == -1) {
        logger_log(logger_id, "[urzednik/przekierowanie] Limit zostal osiagniety", LOG_WARNING);
        tck.ticketid = -1;
        tck.queueid = 0;
      }
      else {
        tck.redirected_from = FACULTY_SA;
        tck.queueid = tck.facultytype + (vip == 1 ? D_MSG_WORKER_PRIORITY_OFFSET : D_MSG_WORKER_OFFSET);
      }

    }
    else if(type != FACULTY_SA && type != CASHIER_POINT && tck.payment == PAYMENT_NOT_NEEDED && rand() % 10 == 0){
      tck.payment = PAYMENT_NEEDED;

      tck.redirected_from = tck.facultytype;
      tck.queueid = CASHIER_POINT + (vip == 1 ? D_MSG_WORKER_PRIORITY_OFFSET : D_MSG_WORKER_OFFSET);
    }
    else if(type == CASHIER_POINT) {
      tck.payment = PAYMENT_SUCCESS;
      tck.facultytype = tck.redirected_from;
      tck.queueid = tck.redirected_from + D_MSG_WORKER_PRIORITY_OFFSET;
    }
    else {
      tck.facultytype = 0;
      tck.queueid = 0;
    }

    // nowy numer biletu
    if(tck.ticketid != -1) {
      sem_lock(urzad->semlock, urzad->semlock_idx);
      tck.ticketid = ++urzad->taken_ticket_count;
      sem_unlock(urzad->semlock, urzad->semlock_idx);
    }

    tck.mtype = tck.requester;
    if(msgsnd(global_msg_id, &tck, sizeof(tck) - sizeof(tck.mtype), 0) == -1) {
      logger_log(logger_id, "[urzednik] Nie mozna wyslac biletu do petenta", LOG_WARNING);
    };

    // zmiejszanie puli dziennych petentow lub koniec pracy (tylko gdy ma sie idx semlocka)
    // struct sembuf op = {limit_semlock_idx, -1, IPC_NOWAIT};
    // if(
    //   (semop(urzad->semlock, &op, 1) == -1 && errno == EAGAIN)
    //   || czy_sygnal_1
    // ){
    //   {
    //     char txt[128];
    //     snprintf(txt, sizeof(txt), "[urzednik] Koncze prace na dzis z mozliwoscia przyjecia max %d", sem_getvalue(urzad->semlock, limit_semlock_idx));
    //     logger_log(logger_id, txt, LOG_INFO);
    //   }
    //   logger_log(logger_id, "[urzednik] Koncze na dzisiaj prace", LOG_INFO);
    // }
    sync_sleep(1);
  }

  logger_log(logger_id, "[urzednik] Zakonczylem prace planowo. Oczekiwanie na zamkniecie gabinetu", LOG_INFO);
  sem_lock(urzad->semlock, queue_semlock_idx);
  // Free remaining people in queue
  {
    msg_ticket_t tck;
    while(msgrcv(global_msg_id, &tck, sizeof(tck) - sizeof(tck.mtype), type + D_MSG_WORKER_PRIORITY_OFFSET, IPC_NOWAIT) != -1){
      tck.mtype = tck.requester;
      tck.ticketid = -1;
      msgsnd(global_msg_id, &tck, sizeof(tck) - sizeof(tck.mtype), 0);
      {
        char txt[256];
        snprintf(txt, sizeof(txt), "[urzednik][%s] Petent nie może zostać przyjęty, bo urzad sie zamyka. (PIORITY PetentPID: %d, BiletID: %d, Sprawa: \"%s\", Wystawił: %d)", faculty_to_str(type), tck.requester, tck.ticketid, faculty_to_str(tck.facultytype), getpid());
        logger_log(logger_id, txt, LOG_INFO);
      }
    }
    while(msgrcv(global_msg_id, &tck, sizeof(tck) - sizeof(tck.mtype), type + D_MSG_WORKER_OFFSET, IPC_NOWAIT) != -1){
      tck.mtype = tck.requester;
      tck.ticketid = -1;
      msgsnd(global_msg_id, &tck, sizeof(tck) - sizeof(tck.mtype), 0);
      {
        char txt[256];
        snprintf(txt, sizeof(txt), "[urzednik][%s] Petent nie może zostać przyjęty, bo urzad sie zamyka. (PetentPID: %d, BiletID: %d, Sprawa: \"%s\", Wystawił: %d)", faculty_to_str(type), tck.requester, tck.ticketid, faculty_to_str(tck.facultytype), getpid());
        logger_log(logger_id, txt, LOG_INFO);
      }
    }
  }
  logger_log(logger_id, "[urzednik] Zamknalem gabinet i wracam do domu", LOG_INFO);

  return 0;
}