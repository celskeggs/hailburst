#ifndef FSW_MAGNETOMETER_H
#define FSW_MAGNETOMETER_H

#include <stdbool.h>

#include <hal/thread.h>
#include <fsw/fakewire/rmap.h>
#include <fsw/tlm.h>

typedef struct {
    rmap_context_t rctx;
    rmap_addr_t address;

    // synchronization
    bool should_be_powered;
    semaphore_t flag_change;

    // telemetry buffer
    queue_t readings;

    // telemetry output endpoint
    tlm_sync_endpoint_t telem_endpoint;

    thread_t query_thread;
    thread_t telem_thread;
} magnetometer_t;

void magnetometer_init(magnetometer_t *mag, rmap_monitor_t *mon, rmap_addr_t *address);
void magnetometer_set_powered(magnetometer_t *mag, bool powered);

#endif /* FSW_MAGNETOMETER_H */
