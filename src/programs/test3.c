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