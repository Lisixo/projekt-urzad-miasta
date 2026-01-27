#include "sync.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>

key_t uniq_key(const char key) {
  key_t k = ftok(".", key);
  return k;
}

// MESSAGE QUEUES

int sync_msg_create(key_t key) {
  return msgget(key, SYNC_PERM | IPC_CREAT | IPC_EXCL);
}

int sync_msg_destroy(int msgid) {
  return msgctl(msgid, IPC_RMID, NULL);
}

// SEMAPHOR

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

int sem_getvalue(int sem_id, unsigned short sem_num) {
  return semctl(sem_id, sem_num, GETVAL);
};

int sem_wait_value(int sem_id, unsigned short sem_num) {
  return semctl(sem_id, sem_num, GETNCNT);
};

int sem_setvalue(int sem_id, unsigned short sem_num, int value) {
  union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
    struct seminfo *__buf;
  } arg;
  arg.val = value;

  return semctl(sem_id, sem_num, SETVAL, arg);
};

int sem_destroy(int sem_id) {
  return semctl(sem_id, 0, IPC_RMID);
};

void sync_sleep(int sec) {
  if(SYNC_SLEEP_ENABLED != 0){
    sleep(sec);
  }
}