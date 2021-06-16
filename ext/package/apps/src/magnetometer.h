#ifndef APP_MAGNETOMETER_H
#define APP_MAGNETOMETER_H

#include <stdbool.h>

#include "rmap.h"
#include "thread.h"
#include "tlm.h"

#define MAGNETOMETER_MAX_READINGS (100)

typedef struct {
    rmap_context_t rctx;
    rmap_addr_t address;

    // synchronization
    pthread_mutex_t mutex;
    pthread_cond_t cond;

    // protected control flag
    bool should_be_powered;

    // protected telemetry buffer
    size_t num_readings;
    tlm_mag_reading_t readings[MAGNETOMETER_MAX_READINGS];

    pthread_t query_thread;
    pthread_t telem_thread;
} magnetometer_t;

void magnetometer_init(magnetometer_t *mag, rmap_monitor_t *mon, rmap_addr_t *address);
void magnetometer_set_powered(magnetometer_t *mag, bool powered);

#endif /* APP_MAGNETOMETER_H */
