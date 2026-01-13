#define KEY_MAIN_LOGGER 'L'
#define KEY_SHM_URZAD 'U'
#define KEY_SHM_PETENT_LIMIT 'P'
#define KEY_SHM_SEMLOCK 'Q'

#define DYREKTOR_MAX_LOBBY 100
#define DYREKTOR_TIME_OPEN 12*60*60
#define DYREKTOR_TIME_CLOSE 16*60*60
#define PETENT_COUNT 1000
#define PETENT_DIFF 100

// shared structures
typedef enum {
    FACULTY_SC = 1,
    FACULTY_KM,
    FACULTY_ML,
    FACULTY_PD,
    FACULTY_SA
} FacultyType;

struct petent_limit {
    int sc, km, ml, pd, sa;
    int semlock;
    int semlock_idx;
};
typedef struct petent_limit petent_limit_t;

struct stan_urzedu {
    int semlock;
    int semlock_idx;

    int time_open; // Tp
    int time_close; // Tk

    int is_opened;

    int lobby_size; // N_MAX
    int current_lobby_size; // N

    int queue_count; // K
};
typedef struct stan_urzedu stan_urzedu_t;