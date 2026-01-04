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

// /**
//  * Tworzy kolejke wiadomosci
//  * @param key identyfikator kolejki
//  */
// int sync_msg_create(key_t key);

// /**
//  * Wysyła wiadomość do kolejki
//  */
// int sync_msg_send(int msgid, const void *msg_ptr, int size);

/**
 * Odbiera wiadomość z kolejki
 */
int sync_msg_receive(int msgid, void *msg_ptr, int size, long type);

/**
 * Niszczy kolejke
 */
int sync_msg_destroy(int msgid);