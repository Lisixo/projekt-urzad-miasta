#define KEY_MAIN_LOGGER 'L'
#define KEY_SHM_URZAD 'U'
#define KEY_SHM_PETENT_LIMIT 'P'

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
    int sc_max, km_max, ml_max, pd_max, sa_max;
    int semlock;
};
typedef struct petent_limit petent_limit_t;

struct stan_urzedu {
    int semlock;

    int time_open; // Tp
    int time_close; // Tk

    int current_time;
    int is_opened;

    int lobby_size; // N_MAX
    int current_lobby_size; // N

    int queue_count; // K
};