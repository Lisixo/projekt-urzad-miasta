#include <time.h>
#include <pthread.h>

#define KEY_MAIN_LOGGER 'L'
#define KEY_SHM_URZAD 'U'
#define KEY_SEM_SEMLOCK 'Q'
#define KEY_MSG_GLOBAL 'G'

#define D_MSG_TICKET_OFFSET 0
#define D_MSG_WORKER_OFFSET 10
#define D_MSG_WORKER_PRIORITY_OFFSET 20

#define DYREKTOR_MAX_LOBBY 500
#define PETENT_COUNT 10000

// time = hours * 60 + minutes
// -1 = use buildin time
#define URZAD_TIME_OPEN -1
#define URZAD_TIME_CLOSE -1

// shared structures

#define WORKER_COUNT 7
#define TICKET_MACHINE_COUNT 3

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

struct sems {
  int configured; // czy dyrektor ustalil dzialanie urzedu
  int opened; // czy urzad jest otwarty
  int building; // bramka ograniczajaca ile osob moze wejsc
  // int ticket_queue; // kolejka do maszyn biletowych
  int tickets; // kolejka do otwartych stanowisk biletowych
  
  int sa_queue; // kolejka do sa
  int sc_queue; // kolejka do sc
  int ml_queue; // kolejka do ml
  int pd_queue; // kolejka do pd
  int km_queue; // kolejka do kd
  int cashier_queue; // kolejka do kasy
  
  int sa1_limit; // licznik limitu dziennych przyjec
  int sa2_limit; // licznik limitu dziennych przyjec
  int sc_limit; // licznik limitu dziennych przyjec
  int ml_limit; // licznik limitu dziennych przyjec
  int pd_limit; // licznik limitu dziennych przyjec
  int km_limit; // licznik limitu dziennych przyjec
  int cashier_limit; // licznik limitu dziennych przyjec
};
typedef struct sems sems_t;

struct stan_urzedu {
  int semlock;
  int semlock_idx;

  sems_t sems;

  time_t time_open; // Tp
  time_t time_close; // Tk

  int is_opened; // Czy urzad jest otwarty
  int is_locked;
  int building_max;
  int taken_ticket_count;

  pid_t dyrektor_pid;

  pid_t urzednicy_pids[WORKER_COUNT];
  pid_t rejestracja_pids[TICKET_MACHINE_COUNT];
  pid_t petent_pids[PETENT_COUNT];
  int petent_pids_limit;
};
typedef struct stan_urzedu stan_urzedu_t;

struct msg_ticket{
  long mtype; // identyfikator kolejki
  pid_t requester; // nadawca
  int ticketid; // numer biletu
  FacultyType facultytype; // typ biletu
  int queueid; // kierunek
  PaymentStatus payment; // inforamcje o platnosci
  FacultyType redirected_from; // przekierowano przez
};
typedef struct msg_ticket msg_ticket_t;