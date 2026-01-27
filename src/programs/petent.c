#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/signal.h>
#include <sys/errno.h>
#include "helpers/consts.h"
#include "helpers/sync.h"
#include "helpers/logger.h"
#include "helpers/utils.h"

volatile sig_atomic_t sig2;

// Sygnaly dla exception_check()
typedef enum {
  NOTHING,
  EXIT_ERROR,
  EXIT_NORMAL
} ExceptionSignal;

struct pthread_control {
  int stop;
  pthread_mutex_t lock;
};
typedef struct pthread_control pthread_control_t;

struct petent {
  pid_t pid;
  FacultyType sprawa;
  int numer_biletu;
  int ma_dziecko;
  pthread_t dziecko;
  pthread_control_t dziecko_controller;
  int arrive_time;
  int vip;
};
typedef struct petent petent_t;

void* dziecko_main(void* arg) {
  pthread_control_t* c = (pthread_control_t*)arg;
  int should_stop = 0;

  while(1) {
    pthread_mutex_lock(&c->lock);
    if(c->stop != 0){
      should_stop = 1;
    }
    pthread_mutex_unlock(&c->lock);

    if(should_stop == 1) {
      break;
    }

    sync_sleep(1);
  }
  return 0;
}

void shutdown(petent_t *p, int signal) {
  if(p->ma_dziecko == 1){
    pthread_mutex_lock(&p->dziecko_controller.lock);
    p->dziecko_controller.stop = 1;
    pthread_mutex_unlock(&p->dziecko_controller.lock);

    pthread_join(p->dziecko, NULL);
  }
  exit(signal);
}

void signal2handler(int sig) {
  sig2 = 1;
}

int exception_check() {

}

int main() {
  // random seed based on getpid() because time(NULL) will be same for all customers
  srand(getpid());
  signal(SIGUSR1, SIG_IGN);
  signal(SIGUSR2, signal2handler);

  // attach logger
  int logger_id = msgget(uniq_key(KEY_MAIN_LOGGER), SYNC_PERM);
  if(logger_id == -1) {
    perror("[petent/init] failed to open logger");
    exit(1);
  }

  // init primary data
  petent_t petent;

  petent.ma_dziecko = 0;
  petent.pid = getpid();
  petent.sprawa = (FacultyType)((rand() % 5) + 1);
  petent.numer_biletu = -1;
  
  if(rand() % 200 == 0) {
    petent.vip = 1;
  }

  // kid setup
  if(rand() % 200 == 0) {
    petent.ma_dziecko = 1;

    petent.dziecko_controller.stop = 0;
    pthread_mutex_init(&petent.dziecko_controller.lock, NULL);

    pthread_create(&petent.dziecko, NULL, dziecko_main, &petent.dziecko_controller);
  } else {
    petent.ma_dziecko = 0;
  }
  
  // attach stan_urzedu
  stan_urzedu_t *urzad;
  {
    int stan_urzedu_shmid = shmget(uniq_key(KEY_SHM_URZAD), sizeof(stan_urzedu_t), SYNC_PERM);
    if(stan_urzedu_shmid == -1) {
      logger_log(logger_id, "[petent/init] Nie mozna otworzyc stan_urzedu", LOG_ERROR);
      shutdown(&petent, 1);
    }
    urzad = shmat(stan_urzedu_shmid, NULL, 0);
  }

  // attach urzednik msg
  int global_msg_id = msgget(uniq_key(KEY_MSG_GLOBAL), SYNC_PERM);
  if(global_msg_id == -1) {
    logger_log(logger_id, "[petent/init] Nie mozna otworzyc global MSG", LOG_ERROR);
    shutdown(&petent, 1);
  }

  // wait for urzad setup
  if(sem_lock(urzad->semlock, urzad->sems.configured) == -1) {
    logger_log(logger_id, "[petent] Nie moge znalesc informacji o godzine otwarcia urzedu", LOG_ERROR);
    shutdown(&petent, 1);
  };
  sem_unlock(urzad->semlock, urzad->sems.configured);
  
  // create waiting time
  time_t t = (urzad->time_close - urzad->time_open);
  time_t arrive_time = time(NULL) + (rand() % t);
  struct tm tm = *localtime(&arrive_time);
  
  {
    char txt[256];
    snprintf(txt, sizeof(txt), "[petent] Dzisiaj musze zalatwic sprawe w %s. Przyjde do urzedu okolo %02d:%02d", faculty_to_str(petent.sprawa), tm.tm_hour, tm.tm_min);
    logger_log(logger_id, txt, LOG_DEBUG);
  }

  // wait for opening
  if(sem_lock(urzad->semlock, urzad->sems.opened) == -1) {
    logger_log(logger_id, "[petent] Chyba urzad dzisiaj wogole nie pracuje. Wracam do domu", LOG_ERROR);
    shutdown(&petent, 1);
  };
  sem_unlock(urzad->semlock, urzad->sems.opened);

  if(sem_lock(urzad->semlock, urzad->semlock_idx) == -1) {
    sem_lock(urzad->semlock, urzad->semlock_idx);
    int urzad_is_opened = urzad->is_opened;
    sem_unlock(urzad->semlock, urzad->semlock_idx);

    if(errno == EINTR && urzad_is_opened == 0) {
      logger_log(logger_id, "Urzad zostal zamkniety zanim wszedlem", LOG_INFO);
      shutdown(&petent, 0);
    }
    logger_log(logger_id, "Urzad zostal zamkniety zanim wszedlem", LOG_INFO);
    shutdown(&petent, 1);
  }
  int urzad_is_opened = urzad->is_opened;
  sem_unlock(urzad->semlock, urzad->semlock_idx);

  //wait for arrival
  {
    time_t t = urzad->time_open - time(NULL);
    if((int)t > 0){
      sync_sleep((int)t);
    }
  }

  // try go into building
  int people_count = petent.ma_dziecko ? 2 : 1;
  {
    if(sem_lock_multi(urzad->semlock, urzad->sems.building, people_count) == -1) {
      logger_log(logger_id, "[petent] Nie moge zajac miejsca w budynku. Wracam do domu", LOG_ERROR);
      shutdown(&petent, 0);
    }
  }

  // entered the building
  logger_log(logger_id, "[petent] Wszedlem do budynku i ide sie ustawic w kolejce po bilet", LOG_DEBUG);

  msg_ticket_t tck;
  tck.mtype = 1;
  tck.facultytype = petent.sprawa;
  tck.requester = petent.pid;
  tck.ticketid = 0;
  tck.payment = PAYMENT_NOT_NEEDED;
  tck.redirected_from = petent.vip;

  // PETENT <---> REJESTRACJA
  {
    // ustawienie sie do kolejki na bilet i oczekiwanie na jego pobranie
    sem_lock(urzad->semlock, urzad->sems.tickets);

    if(msgsnd(global_msg_id, &tck, sizeof(tck) - sizeof(tck.mtype), 0) == -1){
      logger_log(logger_id, "[petent/ticket] Nie moge skorzystac z biletomatu. Wracam do domu sfustrowany", LOG_ERROR);
      sem_unlock_multi(urzad->semlock, urzad->sems.building, people_count);
      sem_unlock(urzad->semlock, urzad->sems.tickets);
      shutdown(&petent, 1);
    }
  
    logger_log(logger_id, "[petent/ticket] Czekam w kolejce bilet", LOG_INFO);
  
    if(msgrcv(global_msg_id, &tck, sizeof(tck) - sizeof(tck.mtype), (long)petent.pid, 0) == -1) {
      sem_lock(urzad->semlock, urzad->semlock_idx);
      int urzad_is_opened = urzad->is_opened;
      sem_unlock(urzad->semlock, urzad->semlock_idx);
  
      // Logika sygnalu 2
      if(sig2) {
        logger_log(logger_id, "[petent/signal] Dostalem sygnal zeby uciekac z budynku", LOG_INFO);
        sem_unlock_multi(urzad->semlock, urzad->sems.building, people_count);
        sem_unlock(urzad->semlock, urzad->sems.tickets);
        shutdown(&petent, 0);
      }
      // Logika zamkniecia urzedu w trakcie zalatwiania sprawy
      else if(urzad_is_opened == 0) {
        logger_log(logger_id, "[petent] Urzad sie zamknal zanim zalatwilem swoja sprawe", LOG_INFO);
        sem_unlock_multi(urzad->semlock, urzad->sems.building, people_count);
        sem_unlock(urzad->semlock, urzad->sems.tickets);
        shutdown(&petent, 1);
      }
  
      // Logika wylaczania
      logger_log(logger_id, "[petent/ticket] Nie moge odebrac biletu", LOG_ERROR);
      sem_unlock_multi(urzad->semlock, urzad->sems.building, people_count);
      sem_unlock(urzad->semlock, urzad->sems.tickets);
      shutdown(&petent, 1);
    }
  
    // Petent wraca do domu, gdy nie dostanie biletu
    if(tck.ticketid == -1) {
      logger_log(logger_id, "[petent/ticket] Limit biletow zostal wyczerpany. Wracam do domu", LOG_ERROR);
      // Zwolnienie miejsca w lobby
        if(sem_unlock_multi(urzad->semlock, urzad->sems.building, people_count) == -1) {
          logger_log(logger_id, "[petent] Nie moge wyjsc z urzedu. Niech ktos mnie wypusci", LOG_WARNING);
        }
      sem_unlock(urzad->semlock, urzad->sems.tickets);
      shutdown(&petent, 0);
    }
  
    logger_log(logger_id, "[petent/ticket] Otrzymalem bilet", LOG_DEBUG);
    // Oddalenie sie z kolejki i oczekiwanie na urzednikow
    sem_unlock(urzad->semlock, urzad->sems.tickets);
  }

  tck.mtype = tck.queueid;

  // PETENT <---> URZEDNIK --> ...
  while(tck.facultytype != 0) {

    // Udanie sie do urzednika
    if(msgsnd(global_msg_id, &tck, sizeof(tck) - sizeof(tck.mtype), 0) == -1) {
      logger_log(logger_id, "[petent/urzednik] Nie moge sie udac do urzednika z biletu", LOG_ERROR);
      shutdown(&petent, 1);
    }

    // Oczekiwanie na wejscie i oczekiwanie na zalatwienie sprawy lub otrzymanie nowego biletu
    if(msgrcv(global_msg_id, &tck, sizeof(tck) - sizeof(tck.mtype), (long)petent.pid, 0) == -1) {
      sem_lock(urzad->semlock, urzad->semlock_idx);
      int urzad_is_opened = urzad->is_opened;
      sem_unlock(urzad->semlock, urzad->semlock_idx);

      // Logika sygnalu 2
      if(sig2) {
        logger_log(logger_id, "[petent/signal] Dostalem sygnal zeby uciekac z budynku", LOG_INFO);
        sem_unlock_multi(urzad->semlock, urzad->sems.building, people_count);
        shutdown(&petent, 0);
      }
      // Logika zamkniecia urzedu w trakcie zalatwiania sprawy
      else if(urzad_is_opened == 0) {
        sem_unlock_multi(urzad->semlock, urzad->sems.building, people_count); // zwalnianie miejsca w budynku

        logger_log(logger_id, "[petent] Urzad sie zamknal zanim zalatwilem swoja sprawe", LOG_INFO);
        sync_sleep(120);
        logger_log(logger_id, "[petent] Jestem sfustrowany", LOG_INFO);
      }
      // Logika wylaczania
      logger_log(logger_id, "[petent/urzednik] Nie moge otrzymac nowego biletu od urzednika", LOG_ERROR);
      shutdown(&petent, 1);
    }

    {
      char txt[128];
      snprintf(txt, sizeof(txt), "[petent/przekierowanie] Zostalem przekierowany z %s do %s %d", faculty_to_str(tck.facultytype), faculty_to_str(tck.queueid % 10), tck.queueid);
      logger_log(logger_id, txt, LOG_INFO);
    }

    if(tck.ticketid == -1) {
      logger_log(logger_id, "[petent/przekierowanie] Limit biletow zostal wyczerpany. Wracam do domu", LOG_ERROR);
      // Zwolnienie miejsca w lobby
      if(sem_unlock_multi(urzad->semlock, urzad->sems.building, people_count) == -1) {
        logger_log(logger_id, "[petent] Nie moge wyjsc z urzedu. Niech ktos mnie wypusci", LOG_WARNING);
      }
      shutdown(&petent, 0);
    }

    tck.mtype = tck.queueid;
  }

  logger_log(logger_id, "[petent] Udalo mi sie wszytsko zalatwic", LOG_INFO);

  // leave the building
  {
    if(sem_unlock_multi(urzad->semlock, urzad->sems.building, people_count) == -1) {
      logger_log(logger_id, "[petent] Nie moge wyjsc z urzedu. Niech ktos mnie wypusci", LOG_WARNING);
    }
  }

  // closing logic
  logger_log(logger_id, "[petent] Wyszedlem z urzedu", LOG_DEBUG);
  shutdown(&petent, 0);
  return 0;
}