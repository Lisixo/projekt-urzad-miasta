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