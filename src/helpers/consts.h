#define KEY_MAIN_LOGGER 'L'
#define KEY_SHM_URZAD 'U'
#define KEY_SHM_PETENT_LIMIT 'P'
#define KEY_SHM_SEMLOCK 'Q'
#define KEY_SEM_LIMITS 'R'

#define DYREKTOR_MAX_LOBBY 10
#define PETENT_COUNT 100

// shared structures

typedef enum {
  FACULTY_SC = 1,
  FACULTY_KM,
  FACULTY_ML,
  FACULTY_PD,
  FACULTY_SA
} FacultyType;

struct petent_limit {
  int sc, km, ml, pd, sa1, sa2; // index semafora dla wydzialu
  int lobby; // index semafora dla lobby

  int sc_max, km_max, ml_max, pd_max, sa_max; // ustawione limity przy starcie
  int lobby_max; // ustawiony limit dla lobby

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
};
typedef struct stan_urzedu stan_urzedu_t;

struct petent {
  pid_t pid;
  FacultyType sprawa;
  int numer_biletu;
  int ma_dziecko;
  pthread_t dziecko;
  int arrive_time;
};
typedef struct petent petent_t;