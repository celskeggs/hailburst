#include <fsw/clock.h>
#include <fsw/clock_init.h>
#include <fsw/tlm.h>

static tlm_async_endpoint_t telemetry;

// no adjustment needed on FreeRTOS.
void clock_init(rmap_monitor_t *mon, rmap_addr_t *address) {
    (void) mon;
    (void) address;

    tlm_async_init(&telemetry);

    tlm_clock_calibrated(&telemetry, 0);
}
