// #include <sys/types.h>
#include <sys/ipc.h>

#define SYNC_PERM 0600
#define SYNC_MSG_SIZE 256

typedef struct {
  int mtype;
  char mtext[SYNC_MSG_SIZE];
} send_msg_t;

/**
 * Tworzy unikalny klucz
 */
key_t uniq_key(const char key);

// MESSAGE QUEUES

/**
 * Odbiera wiadomość z kolejki
 */
int sync_msg_receive(int msgid, void *msg_ptr, int size, long type);

/**
 * Niszczy kolejke
 */
int sync_msg_destroy(int msgid);

// SEMAPHOR

int sem_create(key_t id, int size, int default_state);

int sem_lock(int sem_id, unsigned short sem_num);

int sem_unlock(int sem_id, unsigned short sem_num);

int sem_destroy(int sem_id);