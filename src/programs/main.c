#include <stdio.h>
#include "helpers/logger.h"
#include "helpers/sync.h"
#include "helpers/consts.h"
#include "sys/shm.h"
#include <unistd.h>
#include <sys/wait.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <signal.h>

typedef struct {
  Logger* log;
  int semlock_ins;
  int stan_urzedu_shm;
  int petent_limit_shm;
  int rejestracja_msg;
  int urzednicy_msg;
} res_t;

void free_res(res_t* res);

int shutdown(res_t* res) {
  free_res(res);
  exit(1);
}

res_t* alloc_res() {
  res_t* res = malloc(sizeof(res_t));

  // logger init
  res->log = logger_create(uniq_key(KEY_MAIN_LOGGER), "./logs/", LOG_DEBUG);
  if(!res->log) {
    printf("[main/alloc] Nie mozna utworzyc loggera. Zwalnianie zasobow\n");
    shutdown(res);
  }

  // shm semlock init
  res->semlock_ins = sem_create(uniq_key(KEY_SHM_SEMLOCK), 2, 1);
  int curr_semlock_idx = 0;
  if(res->semlock_ins == -1) {
    logger_log(res->log->msgid, "[main/alloc] Nie mozna utworzyc semlock_ins", LOG_ERROR);
    shutdown(res);
  }

  // urzad shm init
  int urzadshmid = shmget(uniq_key(KEY_SHM_URZAD), sizeof(stan_urzedu_t), SYNC_PERM | IPC_CREAT | IPC_EXCL);
  if(urzadshmid == -1){
    logger_log(res->log->msgid, "[main/alloc] Nie mozna utworzyc stan_urzedu SHM. Zwalnianie zasobow\n", LOG_ERROR);
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
    urzad->is_operating = 0;
    urzad->time_close = 0;
    urzad->time_open = 0;

    urzad->ticket_queue = 0;
    
    sem_unlock(urzad->semlock, urzad->semlock_idx);
    shmdt(urzad);
  }
  
  // petent_limit shm init
  int petentlimitshmid = shmget(uniq_key(KEY_SHM_PETENT_LIMIT), sizeof(petent_limit_t), SYNC_PERM | IPC_CREAT | IPC_EXCL);
  if(petentlimitshmid == -1){
    logger_log(res->log->msgid, "[main/alloc] Nie mozna utworzyc petent_limit SHM. Zwalnianie zasobow\n", LOG_ERROR);
    shutdown(res);
  }
  else {
    res->petent_limit_shm = petentlimitshmid;

    petent_limit_t *l;

    l = shmat(res->petent_limit_shm, NULL, 0);

    l->semlock = res->semlock_ins;
    l->semlock_idx = curr_semlock_idx++;

    sem_lock(l->semlock, l->semlock_idx);

    // setup limit semaphor
    int limit_sem_counter = 0;
    l->limit_sem = sem_create(uniq_key(KEY_SEM_LIMITS), 8, 1);
    if(l->limit_sem == -1) {
      logger_log(res->log->msgid, "[main/alloc] Nie mozna utworzyc petent_limit->limit_sem SEM. Zwalnianie zasobow\n", LOG_ERROR);
      shutdown(res);
    }

    union semun {
      int val;
      struct semid_ds *buf;
      unsigned short *array;
    } arg;

    // Setup worker limits
    l->km_max = PETENT_COUNT * 10 / 100;
    l->km = limit_sem_counter++;
    arg.val = l->km_max;
    if(semctl(l->limit_sem, l->km, SETVAL, arg) == -1) {
      logger_log(res->log->msgid, "[main/alloc] Nie mozna zmienic wartosci petent_limit->limit_sem(km) SEM. Zwalnianie zasobow\n", LOG_ERROR);
      shutdown(res);
    }

    l->pd_max = PETENT_COUNT * 10 / 100;
    l->pd = limit_sem_counter++;
    arg.val = l->pd_max;
    if(semctl(l->limit_sem, l->pd, SETVAL, arg) == -1) {
      logger_log(res->log->msgid, "[main/alloc] Nie mozna zmienic wartosci petent_limit->limit_sem(pd) SEM. Zwalnianie zasobow\n", LOG_ERROR);
      shutdown(res);
    }

    l->ml_max = PETENT_COUNT * 10 / 100;
    l->ml = limit_sem_counter++;
    arg.val = l->ml_max;
    if(semctl(l->limit_sem, l->ml, SETVAL, arg) == -1) {
      logger_log(res->log->msgid, "[main/alloc] Nie mozna zmienic wartosci petent_limit->limit_sem(ml) SEM. Zwalnianie zasobow\n", LOG_ERROR);
      shutdown(res);
    }

    l->sc_max = PETENT_COUNT * 10 / 100;
    l->sc = limit_sem_counter++;
    arg.val = l->sc_max;
    if(semctl(l->limit_sem, l->sc, SETVAL, arg) == -1) {
      logger_log(res->log->msgid, "[main/alloc] Nie mozna zmienic wartosci petent_limit->limit_sem(sc) SEM. Zwalnianie zasobow\n", LOG_ERROR);
      shutdown(res);
    }
    
    l->sa_max = PETENT_COUNT * 60 / 100;
    l->sa_max += PETENT_COUNT - (l->km_max + l->pd_max + l->ml_max + l->sc_max + l->sa_max); // align count

    l->sa1 = limit_sem_counter++;
    int sa_half = l->sa_max / 2;
    arg.val = sa_half;
    if(semctl(l->limit_sem, l->sa1, SETVAL, arg) == -1) {
      logger_log(res->log->msgid, "[main/alloc] Nie mozna zmienic wartosci petent_limit->limit_sem(sa1) SEM. Zwalnianie zasobow\n", LOG_ERROR);
      shutdown(res);
    }

    l->sa2 = limit_sem_counter++;
    arg.val = l->sa_max - sa_half;
    if(semctl(l->limit_sem, l->sa2, SETVAL, arg) == -1) {
      logger_log(res->log->msgid, "[main/alloc] Nie mozna zmienic wartosci petent_limit->limit_sem(sa2) SEM. Zwalnianie zasobow\n", LOG_ERROR);
      shutdown(res);
    }

    // setup max lobby size
    // is initialized properly in dyrektor.c when urzad is opened
    l->lobby_max = DYREKTOR_MAX_LOBBY;
    l->lobby = limit_sem_counter++;

    l->ticket_count = 0;
    
    sem_unlock(l->semlock, l->semlock_idx);
    shmdt(l);
  }

  // rejestracja msg init
  res->rejestracja_msg = msgget(uniq_key(KEY_MSG_TICKETS), SYNC_PERM | IPC_CREAT | IPC_EXCL);
  if(res->rejestracja_msg == -1) {
    logger_log(res->log->msgid, "[main/alloc] Nie mozna utworzyc rejestracja MSG. Zwalnianie zasobow\n", LOG_ERROR);
    shutdown(res);
  }

  // urzednik msg init
  res->urzednicy_msg = msgget(uniq_key(KEY_MSG_WORKER), SYNC_PERM | IPC_CREAT | IPC_EXCL);
  if(res->urzednicy_msg == -1) {
    logger_log(res->log->msgid, "[main/alloc] Nie mozna utworzyc urzednicy MSG. Zwalnianie zasobow\n", LOG_ERROR);
    shutdown(res);
  }

  logger_log(res->log->msgid, "[main/alloc] success", LOG_DEBUG);
  return res;
}

void free_res(res_t* res) {
  // free semlock_ins
  if(res->semlock_ins) {
    if(sem_destroy(res->semlock_ins) == -1) {
      logger_log(res->log->msgid, "[main/free] Nie mozna usunac semlock_ins", LOG_ERROR);
    }
  }
  // get petent_limit shm and close internal structures
  {
    petent_limit_t *l;
    l = shmat(res->petent_limit_shm, NULL, 0);

    if(l->limit_sem){
      if(sem_destroy(l->limit_sem) == -1) {
        logger_log(res->log->msgid, "[main/free] Nie mozna usunac petent_limit->limit_sem SEM", LOG_ERROR);
      }
    }

    shmdt(l);
  }
  // free petent_limit_shm
  if(res->petent_limit_shm) {
    if(shmctl(res->petent_limit_shm, IPC_RMID, NULL) == -1){
      logger_log(res->log->msgid, "[main/free] Nie mozna usunac petent_limit shm\n", LOG_ERROR);
    }
  }
  // free stan_urzedu_shm
  if(res->stan_urzedu_shm) {
    if(shmctl(res->stan_urzedu_shm, IPC_RMID, NULL) == -1){
      logger_log(res->log->msgid, "[main/free] Nie mozna usunac stan_urzedu shm\n", LOG_ERROR);
    }
  }
  // free rejestracja_msg
  if(res->rejestracja_msg) {
    if(msgctl(res->rejestracja_msg, IPC_RMID, NULL) == -1){
      logger_log(res->log->msgid, "[main/free] Nie mozna usunac rejestracja msg\n", LOG_ERROR);
    }
  }
  // free urzednicy_msg
  if(res->urzednicy_msg) {
    if(msgctl(res->urzednicy_msg, IPC_RMID, NULL) == -1){
      logger_log(res->log->msgid, "[main/free] Nie mozna usunac urzednicy msg\n", LOG_ERROR);
    }
  }
  
  // destroy logger
  if(res->log) {
    logger_destroy(res->log);
  }
  free(res);
}

// using as global to close when signal received
res_t *res = NULL;

void exit_signal_handler(int sig) {
  char txt[64];
  snprintf(txt, sizeof(txt), "[main/sighandler/%d] Zamykanie aplikacji", sig);
  // kill(0, SIGINT);
  if(res->log){
    logger_log(res->log->msgid, txt, LOG_WARNING);
  } else {
    printf("%s", txt);
  }
  shutdown(res);
}

int generate_customers(res_t* res, int count);

int main() {
  signal(SIGINT, exit_signal_handler);
  res = alloc_res();

  // fork dyrektor
  pid_t dyrektor_pid = fork();
  switch(dyrektor_pid) {
    case -1:
      logger_log(res->log->msgid, "[main/init/dyrektor] Nie mozna utworzyc PID", LOG_ERROR);
      shutdown(res);
      break;
    case 0:
      execl("./dyrektor", "dyrektor", NULL);

      logger_log(res->log->msgid, "[main/init/dyrektor] Nie mozna uruchomic procedury", LOG_ERROR);
      shutdown(res);
      break;
  }

  // fork tickets machines
  for(int i=1; i<=3; i++) {
    pid_t pid = fork();
    switch(pid) {
      case -1:
        logger_log(res->log->msgid, "[main/init/rejestracja] Nie mozna utworzyc PID", LOG_ERROR);
        shutdown(res);
        break;
      case 0:
        char machine_id[16];
        snprintf(machine_id, sizeof(machine_id), "%d", i);
        execl("./rejestracja", "rejestracja", machine_id, (char *)NULL);

        logger_log(res->log->msgid, "[main/init/rejestracja] Nie mozna uruchomic procedury", LOG_ERROR);
        shutdown(res);
        break;
    }
  }

  // fork workers
  petent_limit_t *pt;
  pt = shmat(res->petent_limit_shm, NULL, 0);

  FacultyType workertypes[] = {FACULTY_KM, FACULTY_PD, FACULTY_ML, FACULTY_SC, FACULTY_SA, FACULTY_SA, CASHIER_POINT};
  int flockidx[] = {pt->km, pt->pd, pt->ml, pt->sc, pt->sa1, pt->sa2, 0};

  shmdt(pt);

  for(int i=0; i<6; i++) {
    pid_t pid = fork();
    switch(pid) {
      case -1:
        logger_log(res->log->msgid, "[main/init/urzednik] Nie mozna utworzyc PID", LOG_ERROR);
        shutdown(res);
        break;
      case 0:
        char faculty[4];
        snprintf(faculty, sizeof(faculty), "%d", workertypes[i]);
        char semlckidx[4];
        snprintf(semlckidx, sizeof(semlckidx), "%d", flockidx[i]);
        execl("./urzednik", "urzednik", faculty, semlckidx, (char *)NULL);

        logger_log(res->log->msgid, "[main/init/urzednik] Nie mozna uruchomic procedury", LOG_ERROR);
        shutdown(res);
        break;
    }
  }

  // fork customers
  generate_customers(res, PETENT_COUNT);

  int status;
  pid_t pid;
  while((pid = wait(&status)) > 0){
    char txt[128];
    snprintf(txt, sizeof(txt), "[main/wait] Proces %d zakonczyl swoje dzialanie ze statusem %d", pid, status);
    logger_log(res->log->msgid, txt, LOG_DEBUG);
  }

  free_res(res);
  return 0;
}

int generate_customers(res_t* res, int count) {
  for(int i = 0; i < count; i++) {
    switch(fork()) {
      case -1:
        char txt[64];
        snprintf(txt, sizeof(txt), "[main/init/petent] Nie mozna utworzyc PID");
        logger_log(res->log->msgid, txt, LOG_ERROR);
        shutdown(res);
        break;
      case 0:
        execl("./petent", "petent", NULL);

        logger_log(res->log->msgid, "[main/init/petent] Nie mozna uruchomic procedury", LOG_ERROR);
        shutdown(res);
        break;
    }
  }
}