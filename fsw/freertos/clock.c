#include <fsw/clock.h>
#include <fsw/clock_init.h>
#include <fsw/tlm.h>

static tlm_async_endpoint_t telemetry;

void clock_init(rmap_addr_t *address, chart_t **rx_out, chart_t **tx_out) {
    (void) address;
    *rx_out = NULL;
    *tx_out = NULL;

    tlm_async_init(&telemetry);
}

void clock_start(void) {
    // no adjustment needed on FreeRTOS.
    tlm_clock_calibrated(&telemetry, 0);
}
