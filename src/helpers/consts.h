#include <time.h>
#include <pthread.h>

#define KEY_MAIN_LOGGER 'L'
#define KEY_SHM_URZAD 'U'
#define KEY_SHM_PETENT_LIMIT 'P'
#define KEY_SHM_SEMLOCK 'Q'
#define KEY_SEM_LIMITS 'R'
#define KEY_MSG_TICKETS 'T'
#define KEY_MSG_WORKER 'W'

#define DYREKTOR_MAX_LOBBY 30
#define PETENT_COUNT 1000

// shared structures

typedef enum {
  FACULTY_SC = 1,
  FACULTY_KM,
  FACULTY_ML,
  FACULTY_PD,
  FACULTY_SA,
  CASHIER_POINT
} FacultyType;

typedef enum {
  PAYMENT_NOT_NEEDED = 0,
  PAYMENT_NEEDED,
  PAYMENT_SUCCESS
} PaymentStatus;

struct petent_limit {
  int sc, km, ml, pd, sa1, sa2; // index semafora dla wydzialu
  int lobby; // index semafora dla lobby

  int sc_max, km_max, ml_max, pd_max, sa_max; // ustawione limity przy starcie
  int lobby_max; // ustawiony limit dla lobby

  int ticket_count;

  int limit_sem;

  int semlock;
  int semlock_idx;
};
typedef struct petent_limit petent_limit_t;

struct stan_urzedu {
  int semlock;
  int semlock_idx;

  time_t time_open; // Tp
  time_t time_close; // Tk

  int is_opened; // Czy urzad jest otwarty
  int is_operating; // Czy urzad jest funkcjonujacy (czy dyrektor oglosil godziny otwarcia)

  int ticket_queue;
};
typedef struct stan_urzedu stan_urzedu_t;

struct msg_ticket{
  long mtype; // identyfikator adresata
  pid_t requester; // nadawca
  int ticketid; // numer biletu
  FacultyType facultytype; // adresat
  PaymentStatus payment; // inforamcje o platnosci
  FacultyType redirected_from; // przekierowano przez
};
typedef struct msg_ticket msg_ticket_t;

struct raport_dnia {
  msg_ticket_t **ticket;
  msg_ticket_t **ticket_success;
  msg_ticket_t **ticket_failed;
};
typedef struct raport_dnia raport_dnia_t;