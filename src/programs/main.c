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
  int global_sem;
  int stan_urzedu_shm;
  int raport_shm;
  int global_msg;
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
    free(res);
    exit(1);
  }

  // shm semlock init
  res->global_sem = sem_create(uniq_key(KEY_SHM_SEMLOCK), 30, 1);
  int curr_sem_idx = 0;
  if(res->global_sem == -1) {
    logger_log(res->log->msgid, "[main/alloc] Nie mozna utworzyc global SEM", LOG_ERROR);
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

    urzad->semlock = res->global_sem;
    urzad->semlock_idx = curr_sem_idx++;

    sem_lock(urzad->semlock, urzad->semlock_idx);

    urzad->building_max = DYREKTOR_MAX_LOBBY;
    urzad->time_close = 0;
    urzad->time_open = 0;
    urzad->is_opened = 0;

    urzad->taken_ticket_count = 0;

    sems_t s;
    s.configured = curr_sem_idx++;
    sem_setvalue(urzad->semlock, s.configured, 0);
    s.opened = curr_sem_idx++;
    sem_setvalue(urzad->semlock, s.opened, 0);
    s.building = curr_sem_idx++;
    sem_setvalue(urzad->semlock, s.building, 0);
    s.tickets = curr_sem_idx++;
    sem_setvalue(urzad->semlock, s.tickets, 0);

    s.km_limit = curr_sem_idx++;
    sem_setvalue(urzad->semlock, s.km_limit, PETENT_COUNT * 10 / 100);
    s.km_queue = curr_sem_idx++;
    sem_setvalue(urzad->semlock, s.km_queue, 0);

    s.ml_limit = curr_sem_idx++;
    sem_setvalue(urzad->semlock, s.ml_limit, PETENT_COUNT * 10 / 100);
    s.ml_queue = curr_sem_idx++;
    sem_setvalue(urzad->semlock, s.ml_queue, 0);

    s.pd_limit = curr_sem_idx++;
    sem_setvalue(urzad->semlock, s.pd_limit, PETENT_COUNT * 10 / 100);
    s.pd_queue = curr_sem_idx++;
    sem_setvalue(urzad->semlock, s.pd_queue, 0);

    s.sc_limit = curr_sem_idx++;
    sem_setvalue(urzad->semlock, s.sc_limit, PETENT_COUNT * 10 / 100);
    s.sc_queue = curr_sem_idx++;
    sem_setvalue(urzad->semlock, s.sc_queue, 0);

    int sa_max = PETENT_COUNT * 60 / 100;
    sa_max += PETENT_COUNT - sa_max - (4 * (int)(PETENT_COUNT * 10 / 100)); // align count
    int sa_half = sa_max / 2;

    s.sa1_limit = curr_sem_idx++;
    sem_setvalue(urzad->semlock, s.sa1_limit, sa_half);
    s.sa2_limit = curr_sem_idx++;
    sem_setvalue(urzad->semlock, s.sa2_limit, PETENT_COUNT - sa_half);
    s.sa_queue = curr_sem_idx++;
    sem_setvalue(urzad->semlock, s.sa_queue, 0);

    s.cashier_limit = curr_sem_idx++;
    sem_setvalue(urzad->semlock, s.cashier_limit, PETENT_COUNT);
    s.cashier_queue = curr_sem_idx++;
    sem_setvalue(urzad->semlock, s.cashier_queue, 0);

    urzad->sems = s;
    
    {
      char txt[192];
      snprintf(txt, sizeof(txt), "[main/alloc/urzad] Alokowano %d semaforow", curr_sem_idx);
      logger_log(res->log->msgid, txt, LOG_INFO);
    }

    sem_unlock(urzad->semlock, urzad->semlock_idx);
    shmdt(urzad);
  }
  
  // global msg init
  res->global_msg = msgget(uniq_key(KEY_MSG_GLOBAL), SYNC_PERM | IPC_CREAT | IPC_EXCL);
  if(res->global_msg == -1) {
    logger_log(res->log->msgid, "[main/alloc] Nie mozna utworzyc global MSG. Zwalnianie zasobow\n", LOG_ERROR);
    shutdown(res);
  }

  logger_log(res->log->msgid, "[main/alloc] success", LOG_DEBUG);
  return res;
}

void free_res(res_t* res) {
  // free semlock_ins
  if(res->global_sem != -1) {
    if(sem_destroy(res->global_sem) == -1) {
      logger_log(res->log->msgid, "[main/free] Nie mozna usunac global sem", LOG_ERROR);
    }
  }

  // free stan_urzedu_shm
  if(res->stan_urzedu_shm != -1) {
    if(shmctl(res->stan_urzedu_shm, IPC_RMID, NULL) == -1){
      logger_log(res->log->msgid, "[main/free] Nie mozna usunac stan_urzedu shm\n", LOG_ERROR);
    }
  }

  // free global_msg
  if(res->global_msg != -1) {
    if(msgctl(res->global_msg, IPC_RMID, NULL) == -1){
      logger_log(res->log->msgid, "[main/free] Nie mozna usunac urzednicy msg\n", LOG_ERROR);
    }
  }
  
  // destroy logger
  if(res->log != NULL) {
    logger_destroy(res->log);
  }
  free(res);
}

// using as global to close when signal received
res_t *res = NULL;

void exit_signal_handler(int sig) {
  char txt[64];
  snprintf(txt, sizeof(txt), "[main/sighandler/%d] Zamykanie aplikacji", sig);
  // kill(0, SIGTERM);
  if(res->log){
    logger_log(res->log->msgid, txt, LOG_WARNING);
  } else {
    printf("%s", txt);
  }
  shutdown(res);
}

int generate_customers(res_t* res, int count);
int generate_workers(res_t* res);
int generate_ticket_machines(res_t* res, int count);

int main() {
  signal(SIGINT, exit_signal_handler);
  signal(SIGUSR1, SIG_IGN);
  signal(SIGUSR2, SIG_IGN);
  
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

  stan_urzedu_t *u;
  u = shmat(res->stan_urzedu_shm, NULL, 0);

  if(sem_lock(u->semlock, u->sems.configured) == -1) {
    logger_log(res->log->msgid, "[main/init] Nie moge uzyskac statusu od dyrektora czy jest juz skonfigurowany", LOG_ERROR);
    shutdown(res);
    exit(1);
  };
  sem_unlock(u->semlock, u->sems.configured);

  shmdt(u);

  // fork
  generate_ticket_machines(res, TICKET_MACHINE_COUNT);
  generate_workers(res);
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
  stan_urzedu_t *u;
  u = shmat(res->stan_urzedu_shm, NULL, 0);

  sem_lock(u->semlock, u->semlock_idx);
  
  for(int i = 0; i < count; i++) {
    pid_t pid = fork();
    switch(pid) {
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
      default:
        u->petent_pids[i] = pid;
    }
  }
  
  sem_unlock(u->semlock, u->semlock_idx);
  shmdt(u);
}

int generate_ticket_machines(res_t* res, int count) {
  stan_urzedu_t *u;
  u = shmat(res->stan_urzedu_shm, NULL, 0);

  sem_lock(u->semlock, u->semlock_idx);

  for(int i=0; i<count; i++) {
    pid_t pid = fork();
    switch(pid) {
      case -1:
        logger_log(res->log->msgid, "[main/init/rejestracja] Nie mozna utworzyc PID", LOG_ERROR);
        shutdown(res);
        break;
      case 0:
        char machine_id[11];
        snprintf(machine_id, sizeof(machine_id), "%d", i);
        execl("./rejestracja", "rejestracja", machine_id, (char *)NULL);

        logger_log(res->log->msgid, "[main/init/rejestracja] Nie mozna uruchomic procedury", LOG_ERROR);
        shutdown(res);
        break;
      default:
        u->rejestracja_pids[i] = pid;
    }
  }
  
  sem_unlock(u->semlock, u->semlock_idx);
  shmdt(u);
}

int generate_workers(res_t* res) {
  stan_urzedu_t *u;
  u = shmat(res->stan_urzedu_shm, NULL, 0);

  sem_lock(u->semlock, u->semlock_idx);
  
  FacultyType workertypes[WORKER_COUNT] = {FACULTY_KM, FACULTY_PD, FACULTY_ML, FACULTY_SC, FACULTY_SA, FACULTY_SA, CASHIER_POINT};
  int queueidx[WORKER_COUNT] = {u->sems.km_queue, u->sems.pd_queue, u->sems.ml_queue, u->sems.sc_queue, u->sems.sa_queue, u->sems.sa_queue, u->sems.cashier_queue};
  int limitidx[WORKER_COUNT] = {u->sems.km_limit, u->sems.pd_limit, u->sems.ml_limit, u->sems.sc_limit, u->sems.sa1_limit, u->sems.sa2_limit, 0};

  for(int i=0; i<WORKER_COUNT; i++) {
    pid_t pid = fork();
    switch(pid) {
      case -1:
        logger_log(res->log->msgid, "[main/init/urzednik] Nie mozna utworzyc PID", LOG_ERROR);
        shutdown(res);
        break;
      case 0:
        char faculty[4];
        snprintf(faculty, sizeof(faculty), "%d", workertypes[i]);
        char limit_semlock[4];
        snprintf(limit_semlock, sizeof(limit_semlock), "%d", limitidx[i]);
        char queue_semlock[4];
        snprintf(queue_semlock, sizeof(queue_semlock), "%d", queueidx[i]);
        execl("./urzednik", "urzednik", faculty, queue_semlock, limit_semlock, (char *)NULL);

        logger_log(res->log->msgid, "[main/init/urzednik] Nie mozna uruchomic procedury", LOG_ERROR);
        shutdown(res);
        break;
      default:
        u->urzednicy_pids[i] = pid;
    }
  }
  
  sem_unlock(u->semlock, u->semlock_idx);
  shmdt(u);
}