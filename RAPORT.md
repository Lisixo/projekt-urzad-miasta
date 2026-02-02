# Raport

## Wstep
Projekt ma za zadanie symulować działanie Urzędu Miasta. Symulacja dzieli się na Dyrektora, Rejestracji (jako Biletomatów), Urzędników oraz Petentów. Symulacje przygotowyje program main.

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
Test przedstawia wejscie do budynku, a semafor w nim przedstawia ochroniarza, który wpusza odpowiednią liczbe osób do budynku. 
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