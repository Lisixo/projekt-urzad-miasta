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

struct petent {
  pid_t pid;
  FacultyType sprawa;
  int numer_biletu;
  int ma_dziecko;
  pthread_t dziecko;
  int arrive_time;
  msg_ticket_t *tickets;
  int tickets_size;
};
typedef struct petent petent_t;

void* dziecko_main(void* arg) {
  while(1) {
    //TODO: Dziecko logic
    sleep(1);
  }
  return 0;
}

void shutdown(petent_t *p) {
  if(p->ma_dziecko == 1){
    pthread_cancel(p->dziecko);
    pthread_join(p->dziecko, NULL);
  }
  if(p->tickets != NULL) {
    free(p->tickets);
  }
  if(p != NULL) {
    free(p);
  }
}

int dodaj_bilet(petent_t *p, msg_ticket_t tck) {
  int new_size = p->tickets_size + 1;
  msg_ticket_t *temp = (msg_ticket_t*)realloc(p->tickets, new_size * sizeof(msg_ticket_t));
  if (temp == NULL) return -1;

  p->tickets = temp;
  p->tickets[p->tickets_size] = tck;
  p->tickets_size = new_size;
  return 0;
}

int main() {
  // random seed based on getpid() because time(NULL) will be same for all customers
  srand(getpid());

  // attach logger
  int logger_id = msgget(uniq_key(KEY_MAIN_LOGGER), SYNC_PERM);
  if(logger_id == -1) {
    perror("[petent/init] failed to open logger");
    exit(1);
  }

  // init primary data
  petent_t* petent = malloc(sizeof(petent_t));
  if(petent == NULL){
    logger_log(logger_id, "[petent/init/malloc] Nie mozna przydzielic pamieci", LOG_ERROR);
  }

  petent->ma_dziecko = 0;

  petent->tickets = malloc(sizeof(msg_ticket_t));
  petent->tickets_size = 0;
  if(petent == NULL){
    logger_log(logger_id, "[petent/init/malloc] Nie mozna przydzielic pamieci", LOG_ERROR);
    shutdown(petent);
    exit(1);
  }

  petent->pid = getpid();
  petent->sprawa = (FacultyType)((rand() % 5) + 1);
  petent->numer_biletu = -1;
  
  // kid setup
  if(rand() % 200 == 0) {
    petent->ma_dziecko = 1;
    pthread_create(&petent->dziecko, NULL, dziecko_main, NULL);
  } else {
    petent->ma_dziecko = 0;
  }
  
  // attach stan_urzedu
  stan_urzedu_t *urzad;
  {
    int stan_urzedu_shmid = shmget(uniq_key(KEY_SHM_URZAD), sizeof(stan_urzedu_t), SYNC_PERM);
    if(stan_urzedu_shmid == -1) {
      logger_log(logger_id, "[petent/init] Nie mozna otworzyc stan_urzedu", LOG_ERROR);
      shutdown(petent);
      exit(1);
    }
    urzad = shmat(stan_urzedu_shmid, NULL, 0);
  }

  // attach petent_limit
  petent_limit_t *petentl;
  {
    int petent_limit_shmid = shmget(uniq_key(KEY_SHM_PETENT_LIMIT), sizeof(petent_limit_t), SYNC_PERM);
    if(petent_limit_shmid == -1) {
      logger_log(logger_id, "[petent/init] Nie mozna otworzyc petent_limit", LOG_ERROR);
      shutdown(petent);
      exit(1);
    }
    petentl = shmat(petent_limit_shmid, NULL, 0);
  }
  
  // attach rejestracja msg
  int rejestracjamsgid = msgget(uniq_key(KEY_MSG_TICKETS), SYNC_PERM);
  if(rejestracjamsgid == -1) {
    logger_log(logger_id, "[petent/init] Nie mozna otworzyc rejestracja_msg MSG", LOG_ERROR);
    shutdown(petent);
    exit(1);
  }

  // attach urzednik msg
  int urzednikmsgid = msgget(uniq_key(KEY_MSG_WORKER), SYNC_PERM);
  if(urzednikmsgid == -1) {
    logger_log(logger_id, "[petent/init] Nie mozna otworzyc urzednik_msg MSG", LOG_ERROR);
    shutdown(petent);
    exit(1);
  }

  // wait for urzad setup
  while(urzad->is_operating != 1){
    sleep(5);
  }
  
  // create waiting time
  time_t t = (urzad->time_close - urzad->time_open);
  time_t arrive_time = time(NULL) + (rand() % t);
  struct tm tm = *localtime(&arrive_time);
  
  {
    char txt[256];
    snprintf(txt, sizeof(txt), "[petent] Dzisiaj musze zalatwic sprawe w %s. Przyjde do urzedu okolo %02d:%02d", faculty_to_str(petent->sprawa), tm.tm_hour, tm.tm_min);
    logger_log(logger_id, txt, LOG_DEBUG);
  }

  // printf("\n%d -> %ld : %ld\n", getpid(), time(NULL), arrive_time);
  // wait for opening and arrival
  while(urzad->is_opened != 1 || time(NULL) > arrive_time){
    sleep(5);
  }

  // try go into building
  int people_count = petent->ma_dziecko ? 2 : 1;
  {
    if(sem_lock_multi(petentl->limit_sem, petentl->lobby, people_count) == -1) {
      logger_log(logger_id, "[petent] Nie moge zajac miejsca w lobby. Wracam do domu", LOG_ERROR);
      shutdown(petent);
      return 1;
    }
  }

  // entered the building
  logger_log(logger_id, "[petent] Wszedlem do budynku i ide sie ustawic w kolejce po bilet", LOG_DEBUG);

  // ustawienie sie do kolejki na bilet i oczekiwanie na jego pobranie
  sem_lock(urzad->semlock, urzad->semlock_idx);
  urzad->ticket_queue++;
  sem_unlock(urzad->semlock, urzad->semlock_idx);

  msg_ticket_t tck;
  tck.mtype = 1;
  tck.facultytype = petent->sprawa;
  tck.requester = petent->pid;
  tck.ticketid = 0;
  tck.payment = PAYMENT_NOT_NEEDED;
  tck.redirected_from = 0;
  
  if(msgsnd(rejestracjamsgid, &tck, sizeof(tck) - sizeof(tck.mtype), 0) == -1){
    logger_log(logger_id, "[petent/ticket] Nie mozna wyslac zadania o bilet", LOG_ERROR);
    shutdown(petent);
    exit(1);
  }

  logger_log(logger_id, "[petent/ticket] Czekam w kolejce bilet", LOG_INFO);

  if(msgrcv(rejestracjamsgid, &tck, sizeof(tck) - sizeof(tck.mtype), (long)petent->pid, 0) == -1) {
    logger_log(logger_id, "[petent/ticket] Nie moge odebrac biletu", LOG_ERROR);
    shutdown(petent);
    exit(1);
  }

  // Petent wraca do domu, gdy nie dostanie biletu
  if(tck.ticketid == -1) {
    logger_log(logger_id, "[petent/ticket] Limit biletow zostal wyczerpany. Wracam do domu", LOG_ERROR);
    // Zwolnienie miejsca w lobby
      if(sem_unlock_multi(petentl->limit_sem, petentl->lobby, people_count) == -1) {
        {
          char txt[128];
          snprintf(txt, sizeof(txt), "[petent][PID:%d] Nie moge wyjsc z urzedu. Niech ktos mnie wypusci", petent->pid);
          logger_log(logger_id, txt, LOG_WARNING);
        }
      }
    shutdown(petent);
    exit(0);
  }

  // Przypisanie biletu do petenta i oddalenie sie z kolejki
  if(dodaj_bilet(petent, tck) == -1) {
    logger_log(logger_id, "[petent/ticket] Nie moge zabrac biletu ze soba. Wracam do domu", LOG_ERROR);
    shutdown(petent);
    exit(1);
  }

  logger_log(logger_id, "[petent/ticket] Otrzymalem bilet", LOG_DEBUG);

  sem_lock(urzad->semlock, urzad->semlock_idx);
  urzad->ticket_queue--;
  sem_unlock(urzad->semlock, urzad->semlock_idx);

  tck.mtype = tck.facultytype;

  // Chodzenie po urzednikach
  while(tck.facultytype != 0) {

    // Udanie sie do urzednika
    if(msgsnd(urzednikmsgid, &tck, sizeof(tck) - sizeof(tck.mtype), 0) == -1) {
      logger_log(logger_id, "[petent/urzednik] Nie moge sie udac do urzednika z biletu", LOG_ERROR);
      shutdown(petent);
      exit(1);
    }

    // Oczekiwanie na wejscie i oczekiwanie na zalatwienie sprawy lub otrzymanie nowego biletu
    if(msgrcv(urzednikmsgid, &tck, sizeof(tck) - sizeof(tck.mtype), (long)petent->pid, 0) == -1) {
      logger_log(logger_id, "[petent/urzednik] Nie moge otrzymac nowego biletu od urzednika", LOG_ERROR);
      shutdown(petent);
      exit(1);
    }

    {
      char txt[192];
      snprintf(txt, sizeof(txt), "[petent/p] DEBUG [%ld][%d][%d]", tck.mtype, tck.facultytype, tck.redirected_from);
      logger_log(logger_id, txt, LOG_INFO);
    }

    {
      char txt[128];
      snprintf(txt, sizeof(txt), "[petent/przekierowanie] Zostalem przekierowany do %s", faculty_to_str(tck.mtype));
      logger_log(logger_id, txt, LOG_INFO);
    }

    if(tck.ticketid == -1) {
      logger_log(logger_id, "[petent/przekierowanie] Limit biletow zostal wyczerpany. Wracam do domu", LOG_ERROR);
      // Zwolnienie miejsca w lobby
      if(sem_unlock_multi(petentl->limit_sem, petentl->lobby, people_count) == -1) {
        {
          char txt[128];
          snprintf(txt, sizeof(txt), "[petent][PID:%d] Nie moge wyjsc z urzedu. Niech ktos mnie wypusci", petent->pid);
          logger_log(logger_id, txt, LOG_WARNING);
        }
      }
      shutdown(petent);
      exit(0);
    }

    // zapisz bilety w historii
    if(dodaj_bilet(petent, tck) == -1) {
      logger_log(logger_id, "[petent/ticket] Nie moge zabrac biletu ze soba. Wracam do domu", LOG_ERROR);
      shutdown(petent);
      exit(1);
    }

    tck.mtype = tck.facultytype;
  }

  logger_log(logger_id, "[petent] Udalo mi sie wszytsko zalatwic", LOG_INFO);

  // leave the building
  {
    if(sem_unlock_multi(petentl->limit_sem, petentl->lobby, people_count) == -1) {
      {
        char txt[128];
        snprintf(txt, sizeof(txt), "[petent][PID:%d] Nie moge wyjsc z urzedu. Niech ktos mnie wypusci", petent->pid);
        logger_log(logger_id, txt, LOG_WARNING);
      }
    }
  }

  // closing logic
  logger_log(logger_id, "[petent] Wyszedlem z urzedu", LOG_DEBUG);
  shutdown(petent);
  return 0;
}