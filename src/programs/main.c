#include "stdio.h"
#include "helpers/logger.h"
#include "helpers/sync.h"
#include "helpers/consts.h"

typedef struct {
  Logger* log;
} res_t;

void free_res(res_t* res);

res_t* alloc_res() {
  res_t* res = malloc(sizeof(res_t));

  res->log = logger_create(uniq_key(KEY_MAIN_LOGGER), "./logs/latest.txt", LOG_DEBUG);
  if(!res->log) {
    printf("alloc_res: Nie mozna utworzyc loggera. Natychmiastowe zwalnianie zasobow\n");
    free_res(res);
    exit(1);
  }

  logger_log(res->log->key, "alloc_res: success", LOG_DEBUG);
  return res;
}

void free_res(res_t* res) {
  if(res->log) {
    logger_destroy(res->log);
  }

  free(res);
}

int main() {
  res_t* res = alloc_res();

  

  free_res(res);
  return 0;
}