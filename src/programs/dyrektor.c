#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/signal.h>
#include <errno.h>
#include <string.h>
#include "helpers/consts.h"
#include "helpers/sync.h"
#include "helpers/logger.h"
#include "helpers/utils.h"

void wygeneruj_stan_urzedu(stan_urzedu_t *urzad);

struct raport {
  msg_ticket_t* bilety;
};
typedef struct raport raport_t;

struct pthread_control {
  int msg;
  msg_ticket_t* tickets;
  int ticket_count;
  int running;
  pthread_mutex_t lock;
};
typedef struct pthread_control pthread_control_t;

void* rejected_ticket_loop(void* arg) {
  pthread_control_t* c = (pthread_control_t*)arg;
  int msg;

  pthread_mutex_lock(&c->lock);
  msg = c->msg;
  c->ticket_count = 0;
  c->tickets = malloc(sizeof(msg_ticket_t));
  pthread_mutex_unlock(&c->lock);


  msg_ticket_t tck;
  while(1) {
    pthread_mutex_lock(&c->lock);
    if(c->running == 0){
      pthread_mutex_unlock(&c->lock);
      break;
    }
    pthread_mutex_unlock(&c->lock);

    if(msgrcv(msg, &tck, sizeof(tck) - sizeof(tck.mtype), 2, IPC_NOWAIT) == -1) {
      usleep(500000);
      continue;
    }

    pthread_mutex_lock(&c->lock);
    int new_size = c->ticket_count + 1;
    void* n = realloc(c->tickets, sizeof(msg_ticket_t) * new_size);
    if(n == NULL) {
      fprintf(stderr, "[dyrektor/ticket_loop] Nie mozna dodac ticketu do tablicy z powodu bledy realokacji: %s", strerror(errno));
    }
    else {
      c->tickets = n;
      c->tickets[c->ticket_count] = tck;
      c->ticket_count = new_size;
    }
    pthread_mutex_unlock(&c->lock);

    sync_sleep(1);
  }
  return 0;
}

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

  // attach global msg
  int global_msg_id = msgget(uniq_key(KEY_MSG_GLOBAL), SYNC_PERM);
  if(global_msg_id == -1) {
    logger_log(logger_id, "[dyrektor/init] Nie mozna otworzyc global MSG", LOG_ERROR);
    exit(1);
  }

  pthread_control_t pt_arg;
  pthread_mutex_init(&pt_arg.lock, NULL);
  pt_arg.msg = global_msg_id;
  pt_arg.running = 1;
  pt_arg.ticket_count = 0;
  pt_arg.tickets = malloc(sizeof(msg_ticket_t) * pt_arg.ticket_count);

  if(pt_arg.tickets == NULL) {
    logger_log(logger_id, "[dyrektor/init] Nie mozna alokowac pamieci na bilety", LOG_ERROR);
    exit(1);
  }

  pthread_t pt_thread;
  if(pthread_create(&pt_thread, NULL, rejected_ticket_loop, &pt_arg) != 0){
    logger_log(logger_id, "[dyrektor/init] Nie mozna utworzyc watku", LOG_ERROR);
    free(pt_arg.tickets);
    exit(1);
  }

  // Setup urzad rules
  logger_log(logger_id, "[dyrektor/init] Ustalanie regul urzedu", LOG_INFO);
  if(sem_lock(urzad->semlock, urzad->semlock_idx) == -1){
    logger_log(logger_id, "[dyrektor/init] Nie mozna zablokowac stan_urzedu", LOG_ERROR);
    shmdt(urzad);
    free(pt_arg.tickets);
    pthread_mutex_lock(&pt_arg.lock);
    pt_arg.running = 0;
    pthread_mutex_unlock(&pt_arg.lock);
    pthread_join(pt_thread, NULL);
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
    free(pt_arg.tickets);
    pthread_mutex_lock(&pt_arg.lock);
    pt_arg.running = 0;
    pthread_mutex_unlock(&pt_arg.lock);
    pthread_join(pt_thread, NULL);
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
    free(pt_arg.tickets);
    pthread_mutex_lock(&pt_arg.lock);
    pt_arg.running = 0;
    pthread_mutex_unlock(&pt_arg.lock);
    pthread_join(pt_thread, NULL);
    exit(1);
  }
  else {
    logger_log(logger_id, "[dyrektor] Urzad zostal otwarty", LOG_INFO);
  }

  // WORKING TIME
  while(time(NULL) < urzad->time_close){
    sync_sleep(1);

    // Event 1: Zwolnienie losowego pracownika szybciej do domu
    if(rand() % 2000 == 0) {
      sem_lock(urzad->semlock, urzad->semlock_idx);
      pid_t p = urzad->urzednicy_pids[p % WORKER_COUNT];
      sem_unlock(urzad->semlock, urzad->semlock_idx);

      kill(p, SIGUSR1);
    }

    // // Event 2: Ewakuacja
    if(rand() % 2000 == 0) {
      kill(0, SIGUSR2);
      break;
    }

    // Receive ticket
  }

  // Set close flag
  sem_setvalue(urzad->semlock, urzad->sems.opened, 0);
  sem_lock(urzad->semlock, urzad->semlock_idx);
  urzad->is_opened = 0;
  sem_unlock(urzad->semlock, urzad->semlock_idx);

  // Close lobby
  if(sem_setvalue(urzad->semlock, urzad->sems.building, 0) == -1) {
    logger_log(logger_id, "[dyrektor] Nie mozna zamknac lobby", LOG_ERROR);
    free(pt_arg.tickets);
    pthread_mutex_lock(&pt_arg.lock);
    pt_arg.running = 0;
    pthread_mutex_unlock(&pt_arg.lock);
    pthread_join(pt_thread, NULL);
    exit(1);
  }

  {
    char txt[256];
    snprintf(txt, sizeof(txt), "[dyrektor] Liczba osob oczekujacych przed budynkiem: %d", sem_wait_value(urzad->semlock, urzad->sems.building));
    logger_log(logger_id, txt, LOG_DEBUG);
  }

  // Write raport
  FILE* f = fopen("raport.txt", "w");
  if(f == NULL) {
    logger_log(logger_id, "[dyrektor] Nie mozna otworzyc pliku do raportu", LOG_WARNING);
  }
  else {
    pthread_mutex_lock(&pt_arg.lock);
    struct tm ts = *localtime(&urzad->time_open);
    struct tm tc = *localtime(&urzad->time_close);
    fprintf(f, "Raport %02d:%02d - %02d:%02d\n\nLiczba biletow: %d\n", ts.tm_hour, ts.tm_min, tc.tm_hour, tc.tm_min, pt_arg.ticket_count);
    for(int i = 0; i < pt_arg.ticket_count; i++) {
      msg_ticket_t tck = pt_arg.tickets[i];
      fprintf(f, "-----------\nPetent ID: %d\nBilet ID: %d\nSkierowanie do: %s\nWystawil: %s\n", tck.requester, tck.ticketid, tck.facultytype == 1 ? "Biletomat" : faculty_to_str(tck.facultytype), tck.queueid == 1 ? "Biletomat" : faculty_to_str(tck.queueid));
    }
    fflush(f);
    fclose(f);
    pthread_mutex_unlock(&pt_arg.lock);
  }


  // Close
  pthread_mutex_lock(&pt_arg.lock);
  pt_arg.running = 0;
  pthread_mutex_unlock(&pt_arg.lock);
  pthread_join(pt_thread, NULL);

  kill(0, SIGUSR2);
  free(pt_arg.tickets);
  logger_log(logger_id, "[dyrektor] Zakonczono procedure dyrektor", LOG_INFO);
  return 0;
}

void wygeneruj_stan_urzedu(stan_urzedu_t *urzad) {
  time_t now = time(NULL);
  struct tm* t = localtime(&now);
  time_t offset = (now % (24 * 60 * 60));
  time_t base = now - offset;

  if(URZAD_TIME_OPEN == -1) {    
    // urzad->time_open = now + (((rand() % 5)+1)*60); // open in 1-5 minutes
    urzad->time_open = now + 10; // instant
  }
  else {
    urzad->time_open = base + ((URZAD_TIME_OPEN % 24) * 3600) - t->tm_gmtoff;

    if(urzad->time_open < now){
      urzad->time_open += (24 * 60 * 60);
    }
  }

  if(URZAD_TIME_CLOSE == -1) {
    // urzad->time_close = urzad->time_open + (((rand() % 110)+10)*60); // left open for 10-120 minutes
    urzad->time_close = urzad->time_open + 5*60; // always 5 minutes
  }
  else {
    urzad->time_close = base + ((URZAD_TIME_CLOSE % 24) * 3600) - t->tm_gmtoff;

    if(urzad->time_close < now){
      urzad->time_close += (24 * 60 * 60);
    }
  }
}