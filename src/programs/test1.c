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