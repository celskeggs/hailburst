#ifndef APP_MAGNETOMETER_H
#define APP_MAGNETOMETER_H

#include <stdbool.h>

#include "rmap.h"
#include "thread.h"

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    rmap_context_t rctx;
    rmap_addr_t address;
    bool is_powered;
    bool should_be_powered;
    pthread_t query_thread;
} magnetometer_t;

void magnetometer_init(magnetometer_t *mag, rmap_monitor_t *mon, rmap_addr_t *address);
void magnetometer_set_powered(magnetometer_t *mag, bool powered);

#endif /* APP_MAGNETOMETER_H */
