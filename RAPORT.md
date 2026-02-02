# Raport

## Wstep
Projekt ma za zadanie symulować działanie Urzędu Miasta. Symulacja dzieli się na Dyrektora, Rejestracji (jako Biletomatów), Urzędników oraz Petentów. Symulacje przygotowyje program main bez petentów. Do generowania petentów służy skrypt `generate_petents.sh [COUNT]` gdzie count to liczba petentów.

## Zadania procedur
- main.c
  - Alokacja i zwalnianie pamieci/struktur
  - Uruchamianie procedur oraz pilnowanie żeby wszystkie zostały zakończone
- dyrektor.c
  - Ustala godziny otwarcia
  - Wysyła sygnały
  - Otwiera i zamyka urząd
- rejestracja.c
  - Otwiera się i zamyka w zależności od ilości osób w kolejce
  - Wydaje bilety do urzedników
- urzednik.c
  - Obsluguje petentow
  - Może wydać bilet do innego wydziału lub do kasy
- petent.c
  - Symuluje osobę załatwiającą sprawę w urzędzie
  - Kieruje się najpierw do rejestracji, a potem do urzędników

## Struktury synchronizacji
- Kolejka komunikatów
  - Logger
  - Komunikacja pomiędzy:
    - Petent <---> Rejestracja
    - Petent <---> Urzędnik
    - Urzędnik <---> Dyrektor
    - Rejestracja <---> Dyrektor
- Sygnały
  - Dyrektor z sygnałami SIGUSR1 oraz SIGUSR2
  - Obsługa tych sygnałów w pozostałych procedurach
- Semafory
  - Wejscie do budynku
  - Kolejka do rejestracji oraz urzędników
  - Limity dzienne urzędników
- Mutex
  - W petent oraz dyrektor do synchronizacji danych w wątkach
- Pamięć dzielona
  - Stan Urzędu

## Testy
### Test 1 - Oczekiwanie na wejscie do budynku
Test przedstawia wejscie do budynku, a semafor w nim przedstawia ochroniarza, który wpuszcza odpowiednią liczbe osób do budynku. 
```c
#include <sys/sem.h>
#include <sys/ipc.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include "helpers/sync.h"

int main() {
    int semid = sem_create(IPC_PRIVATE, 1, 0);

    pid_t pid = fork();

    if(pid == 0) {
        // dziecko
        printf("I: Czekam na wejscie\n");
        sem_lock(semid, 0);
        printf("I: Wszedlem\n");
        exit(0);
    }
    else {
        sleep(1);

        int wait_queue = sem_wait_value(semid, 0);
        
        if(sem_wait_value(semid, 0) != 1) {
            printf("[FAIL] Dziecko nie czeka na wejscie\n");
            sem_destroy(semid);
            exit(1);
        }

        sem_unlock(semid, 0);

        int status;
        wait(&status);

        if(status != 0) {
            printf("[FAIL] Dziecko nie zakonczylo sie poprawnie\n");
            sem_destroy(semid);
            exit(1);
        } 

        printf("[PASS] Dziecko poprawnie czekalo na semafor\n");
        sem_destroy(semid);
        exit(0);
    }
}
```

### Test 2 - Test otrzymywania sygnału SIGUSR2
Test sprawdza czy sygnal SIGUSR2 jets odbierany przez osobny proces.
```c
#include <sys/sem.h>
#include <sys/ipc.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include "helpers/sync.h"

volatile sig_atomic_t sig2;
void signal2handler(int sig) {
  sig2 = 1;
}

int main() {
    int semid = sem_create(IPC_PRIVATE, 1, 0);

    pid_t pid = fork();

    if(pid == 0) {
        // dziecko

        signal(SIGUSR2, signal2handler);
        if(sem_lock(semid, 0) == -1 && errno == EINTR && sig2) {
            exit(0);
        }

        exit(1);
    }
    else {
        sleep(1);

        kill(pid, SIGUSR2);

        int status;
        wait(&status);

        if(status != 0) {
            printf("[FAIL] Dziecko nie zakonczylo sie poprawnie\n");
            sem_destroy(semid);
            exit(1);
        } 

        printf("[PASS] Dziecko poprawnie sie zakonczylo z sygnalu SIGUSR2\n");
        sem_destroy(semid);
        exit(0);
    }
}
```

### Test 3 - Test wymiany biletów
Test sprawdza czy bilety są wysyłane i odbierane przez osobne procesy.
```c
#include <sys/sem.h>
#include <sys/msg.h>
#include <sys/ipc.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <errno.h>
#include "helpers/sync.h"
#include "helpers/consts.h"

int main() {
    srand(time(NULL));
    int msgid = sync_msg_create(IPC_CREAT);

    int num_clients = 5;
    int received = 0;

    for(int i=1; i<=num_clients; i++) {
        pid_t p = fork();
        if(p == 0) {
            msg_ticket_t tck;
            tck.facultytype = (rand() % 5) + 1;
            tck.mtype = i;
            tck.payment = PAYMENT_NOT_NEEDED;
            tck.queueid = 0;
            tck.redirected_from = 0;
            tck.requester = getpid();
            tck.ticketid = i;
            
            if(msgsnd(msgid, &tck, sizeof(tck) - sizeof(long), 0) == -1) {
                printf("[FAIL][%d] Nie mozna wyslac wiadomosci\n", getpid());
                exit(1);
            }

            exit(0);
        }
        else if(p == -1){
            printf("[FAIL][%d] Nie mozna pomyslnie wykonac fork()\n", getpid());
            exit(1);
        }
    }

    for(int i=1; i<=num_clients; i++) {
        msg_ticket_t tck;
        if(msgrcv(msgid, &tck, sizeof(tck) - sizeof(long), i, 0) == -1) {
            printf("[FAIL][%d] Nie mozna otrzymac wiadomosci %d\n", getpid(), i);
            sync_msg_destroy(msgid);
            exit(1);
        }

        received += 1;
    }

    if(received == num_clients){
        printf("[PASS] Wszystkie bilety przyszly\n");
        sync_msg_destroy(msgid);
        exit(0);
    }
    else {
        printf("[FAIL] Brakuje biletow\n");
        sync_msg_destroy(msgid);
        exit(1);
    }
}
```

### Test 4 - Test pamięci współdzielonej z blokadą semaforową
Test ma sprawdzić czy w dodawaniu licznika pobranych biletów jest chroniona przed zapisem w tym samym momencie.
```c
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <errno.h>
#include "helpers/sync.h"
#include "helpers/consts.h"

int main() {
    int shmid = shmget(IPC_PRIVATE, sizeof(stan_urzedu_t), SYNC_PERM);
    stan_urzedu_t *u = shmat(shmid, NULL, 0);
    u->taken_ticket_count = 0;

    int semid = sem_create(IPC_PRIVATE, 1, 1);
    u->semlock = semid;
    u->semlock_idx = 0;

    int iterations = 1000;

    pid_t p = fork();
    if(p == 0) {
        for(int i=0; i<iterations; i++) {
            if(sem_lock(u->semlock, u->semlock_idx) == -1) {
                printf("[FAIL] Nie mozna zablokowac semafora\n");
                exit(1);
            }

            u->taken_ticket_count += 1;

            if(sem_unlock(u->semlock, u->semlock_idx) == -1) {
                printf("[FAIL] Nie mozna odblokowac semafora\n");
                exit(1);
            }
        }

        exit(0);
    }
    else if(p == -1){
        printf("[FAIL][%d] Nie mozna pomyslnie wykonac fork()\n", getpid());
        exit(1);
    }

    for(int i=0; i<iterations; i++) {
        if(sem_lock(u->semlock, u->semlock_idx) == -1) {
            printf("[FAIL] Nie mozna zablokowac semafora\n");
            sem_destroy(semid);
            shmctl(shmid, IPC_RMID, NULL);
            exit(1);
        }

        u->taken_ticket_count += 1;

        if(sem_unlock(u->semlock, u->semlock_idx) == -1) {
            printf("[FAIL] Nie mozna odblokowac semafora\n");
            sem_destroy(semid);
            shmctl(shmid, IPC_RMID, NULL);
            exit(1);
        }
    }

    int status;
    wait(&status);

    if(status != 0) {
        printf("[FAIL] Dziecko nie zakonczylo sie poprawnie\n");
        sem_destroy(semid);
        shmctl(shmid, IPC_RMID, NULL);
        exit(1);
    }

    if(u->taken_ticket_count == iterations*2){
        printf("[PASS] Liczba biletow sie zgadza\n");
        sem_destroy(semid);
        shmctl(shmid, IPC_RMID, NULL);
        exit(0);
    } else {
        printf("[FAIL] Liczba biletow sie nie zgadza\n");
        sem_destroy(semid);
        shmctl(shmid, IPC_RMID, NULL);
        exit(1);
    }
}
```

### Użycia funkcji systemowych
#### Obsługa plików
- fopen (w helpers/logger.c) [Link](https://github.com/Lisixo/projekt-urzad-miasta/blob/b2eccbb5b47602303ee49d3a639757f1f7c87c91/src/helpers/logger.c#L80-L105)
```c
  // init latest file
  char latestfilepath[512];
  snprintf(latestfilepath, sizeof(latestfilepath), "%slatest.txt", rootpath);

  logger->latestfile = fopen(latestfilepath, "w");

  if(!logger->latestfile) {
    perror("logger_create: failed to open latestfile ::");
    sync_msg_destroy(logger->msgid);
    free(logger);
    return NULL;
  }

  // init latest file
  char historyfilepath[512];
  snprintf(historyfilepath, sizeof(historyfilepath), "%s%ld.txt", rootpath, (long)time(NULL));

  logger->historyfile = fopen(historyfilepath, "w");

  if(!logger->historyfile) {
    perror("logger_create: failed to open historyfile ::");
    fclose(logger->latestfile);
    sync_msg_destroy(logger->msgid);
    free(logger);
    return NULL;
  }
```
- fclose (w helpers/logger.c) [Link](https://github.com/Lisixo/projekt-urzad-miasta/blob/b2eccbb5b47602303ee49d3a639757f1f7c87c91/src/helpers/logger.c#L174-L189)
```c
  void logger_destroy(Logger* logger) {
  if (!logger)
    return;
    
  pthread_mutex_lock(&logger->controller.lock);
  logger->controller.stop = 1;
  pthread_mutex_unlock(&logger->controller.lock);
  
  logger_log(logger->msgid, "logger thread stoped", LOG_DEBUG);
    
  pthread_join(logger->thread, NULL);
  sync_msg_destroy(logger->msgid);
  fclose(logger->latestfile);
  fclose(logger->historyfile);
  free(logger);
}
```
#### Tworzenie procesów
- fork oraz execl (w main.c) [Link](https://github.com/Lisixo/projekt-urzad-miasta/blob/b2eccbb5b47602303ee49d3a639757f1f7c87c91/src/programs/main.c#L222-L235)
```c
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
```
- wait (w main.c) [Link](https://github.com/Lisixo/projekt-urzad-miasta/blob/b2eccbb5b47602303ee49d3a639757f1f7c87c91/src/programs/main.c#L253-L259)
```c
int status;
pid_t pid;
while((pid = wait(&status)) > 0){
  char txt[128];
  snprintf(txt, sizeof(txt), "[main/wait] Proces %d zakonczyl swoje dzialanie ze statusem %d", pid, status);
  logger_log(res->log->msgid, txt, LOG_DEBUG);
}
```
#### Tworzenie i obsługa wątków
- pthread_create (w helpers/logger.c) [Link](https://github.com/Lisixo/projekt-urzad-miasta/blob/b2eccbb5b47602303ee49d3a639757f1f7c87c91/src/helpers/logger.c#L111-L119)
```c
if(pthread_create(&logger->thread, NULL, logger_loop, logger) != 0) {
  perror("logger_create: failed to create THREAD ::");

  fclose(logger->latestfile);
  fclose(logger->historyfile);
  sync_msg_destroy(logger->msgid);
  free(logger);
  return NULL;
}
```
- pthread_mutex (w wątku petent.c) [Link](https://github.com/Lisixo/projekt-urzad-miasta/blob/b2eccbb5b47602303ee49d3a639757f1f7c87c91/src/programs/petent.c#L45-L63)
```c
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
```
- pthread_join (w funkcji shutdown() petent.c) [Link](https://github.com/Lisixo/projekt-urzad-miasta/blob/b2eccbb5b47602303ee49d3a639757f1f7c87c91/src/programs/petent.c#L65-L74)
```c
void shutdown(petent_t *p, int signal) {
  if(p->ma_dziecko == 1){
    pthread_mutex_lock(&p->dziecko_controller.lock);
    p->dziecko_controller.stop = 1;
    pthread_mutex_unlock(&p->dziecko_controller.lock);

    pthread_join(p->dziecko, NULL);
  }
  exit(signal);
}
```

#### Obsługa sygnałów
- signal (w petent.c) [Link](https://github.com/Lisixo/projekt-urzad-miasta/blob/b2eccbb5b47602303ee49d3a639757f1f7c87c91/src/programs/petent.c#L110-L115)
```c
int main() {
// random seed based on getpid() because time(NULL) will be same for all customers
srand(getpid());
signal(SIGUSR1, SIG_IGN);
signal(SIGUSR2, signal2handler);
signal(SIGINT, signal2handler);
```

- kill (w dyrektor.c) [Link](https://github.com/Lisixo/projekt-urzad-miasta/blob/b2eccbb5b47602303ee49d3a639757f1f7c87c91/src/programs/dyrektor.c#L215-L221)
```c
for(int i=0; i < urzad->petent_pids_limit; i++) {
  if(urzad->petent_pids[i] != 0) {
    kill(urzad->petent_pids[i], SIGUSR2);
    urzad->petent_pids[i] = 0;
  }
}
```

#### Semafory
- semget (w helpers/sync.c jako funkcja pomocnicza) [Link](https://github.com/Lisixo/projekt-urzad-miasta/blob/b2eccbb5b47602303ee49d3a639757f1f7c87c91/src/helpers/sync.c#L27-L49)
```c
int sem_create(key_t id, int size, int default_state) {
  int semid = semget(id, size, IPC_CREAT | IPC_EXCL | SYNC_PERM);

  if(semid == -1) {
    return -1;
  }

  union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
  } arg;
  arg.val = default_state;

  for(int i=0; i<size; i++){
    if(semctl(semid, i, SETVAL, arg) == -1) {
      sem_destroy(semid);
      return -1;
    }
  }

  return semid;
};
```
- semop (w helpers/sync.c jako funkcja pomocnicza) [Link](https://github.com/Lisixo/projekt-urzad-miasta/blob/b2eccbb5b47602303ee49d3a639757f1f7c87c91/src/helpers/sync.c#L51-L67)
```
int sem_lock(int sem_id, unsigned short sem_num) {
  return sem_lock_multi(sem_id, sem_num, 1);
};

int sem_unlock(int sem_id, unsigned short sem_num) {
  return sem_unlock_multi(sem_id, sem_num, 1);
};

int sem_lock_multi(int sem_id, unsigned short sem_num, int count) {
  struct sembuf op = {sem_num, count * -1, 0};
  return semop(sem_id, &op, 1);
};

int sem_unlock_multi(int sem_id, unsigned short sem_num, int count) {
  struct sembuf op = {sem_num, count, 0};
  return semop(sem_id, &op, 1);
};
```

#### Pamięć dzielona
- shmget oraz shmat (w dyrektor.c) [Link](https://github.com/Lisixo/projekt-urzad-miasta/blob/b2eccbb5b47602303ee49d3a639757f1f7c87c91/src/programs/dyrektor.c#L94-L103)
```c
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
```
- shmctl (w main.c) [Link](https://github.com/Lisixo/projekt-urzad-miasta/blob/b2eccbb5b47602303ee49d3a639757f1f7c87c91/src/programs/main.c#L164-L169)
```c
// free stan_urzedu_shm
if(res->stan_urzedu_shm != -1) {
  if(shmctl(res->stan_urzedu_shm, IPC_RMID, NULL) == -1){
    logger_log(res->log->msgid, "[main/free] Nie mozna usunac stan_urzedu shm\n", LOG_ERROR);
  }
}
```
- shmdt (w main.c) [Link](https://github.com/Lisixo/projekt-urzad-miasta/blob/b2eccbb5b47602303ee49d3a639757f1f7c87c91/src/programs/main.c#L198-L206)
```c
if(res->stan_urzedu_shm){
  stan_urzedu_t* u;
  u = shmat(res->stan_urzedu_shm, NULL, 0);
  if(kill(u->dyrektor_pid, 0) == 0) {
    kill(u->dyrektor_pid, SIGINT);
    shmdt(u);
  }
  return;
}
```

#### Kolejki komunikatów
- msgget (w dyrektor.c) [Link](https://github.com/Lisixo/projekt-urzad-miasta/blob/b2eccbb5b47602303ee49d3a639757f1f7c87c91/src/programs/dyrektor.c#L105-L110)
```c
// attach global msg
int global_msg_id = msgget(uniq_key(KEY_MSG_GLOBAL), SYNC_PERM);
if(global_msg_id == -1) {
  logger_log(logger_id, "[dyrektor/init] Nie mozna otworzyc global MSG", LOG_ERROR);
  exit(1);
}
```

- msgrcv (w uzrednik.c) [Link](https://github.com/Lisixo/projekt-urzad-miasta/blob/b2eccbb5b47602303ee49d3a639757f1f7c87c91/src/programs/urzednik.c#L106-L123)
```c
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
```
- msgsnd (w urzednik.c) [Link](https://github.com/Lisixo/projekt-urzad-miasta/blob/b2eccbb5b47602303ee49d3a639757f1f7c87c91/src/programs/urzednik.c#L202-L206)
```c
// Send response to petent
tck.mtype = tck.requester;
if(msgsnd(global_msg_id, &tck, sizeof(tck) - sizeof(tck.mtype), 0) == -1) {
  logger_log(logger_id, "[urzednik] Nie mozna wyslac biletu do petenta", LOG_WARNING);
};
```
- msgctl (w main.c) [Link](https://github.com/Lisixo/projekt-urzad-miasta/blob/b2eccbb5b47602303ee49d3a639757f1f7c87c91/src/programs/main.c#L171-L176)
```c
// free global_msg
if(res->global_msg != -1) {
  if(msgctl(res->global_msg, IPC_RMID, NULL) == -1){
    logger_log(res->log->msgid, "[main/free] Nie mozna usunac urzednicy msg\n", LOG_ERROR);
  }
}
```
