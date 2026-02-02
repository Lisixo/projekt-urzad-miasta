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