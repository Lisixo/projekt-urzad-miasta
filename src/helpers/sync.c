#include "sync.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

key_t uniq_key(const char key) {
  key_t k = ftok(".", key);
  return k;
}

int sync_msg_create(key_t key) {
  int msgid = msgget(key, SYNC_PERM | IPC_CREAT);

  if(msgid == -1) {
    perror("sync_msg_create: failed to create msg queue");
    return -1;
  }

  return msgid;
}

// int sync_msg_send(int msgid, const void *msg_ptr, int size) {
// }

// int sync_msg_receive(int msgid, void *msg_ptr, int size, long type) {

// }

int sync_msg_destroy(int msgid) {
  return msgctl(msgid, IPC_RMID, NULL);
}