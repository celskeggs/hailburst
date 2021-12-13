#ifndef FSW_MAGNETOMETER_H
#define FSW_MAGNETOMETER_H

#include <stdbool.h>

#include <hal/thread.h>
#include <fsw/fakewire/rmap.h>
#include <fsw/fakewire/switch.h>
#include <fsw/tlm.h>

typedef struct {
    rmap_t      endpoint;
    rmap_addr_t address;

    // synchronization
    bool should_be_powered;
    semaphore_t flag_change;

    // telemetry buffer
    chart_t readings;

    // telemetry output endpoint
    tlm_async_endpoint_t telemetry_async;
    tlm_sync_endpoint_t  telemetry_sync;

    thread_t query_thread;
    thread_t telem_thread;
} magnetometer_t;

void magnetometer_init(magnetometer_t *mag, rmap_addr_t *address, chart_t **rx_out, chart_t **tx_out);
void magnetometer_set_powered(magnetometer_t *mag, bool powered);

#endif /* FSW_MAGNETOMETER_H */
