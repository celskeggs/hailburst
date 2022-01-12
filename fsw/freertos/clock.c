#include <hal/thread.h>
#include <fsw/clock.h>
#include <fsw/clock_init.h>
#include <fsw/telemetry.h>

static tlm_async_endpoint_t telemetry;

void clock_init(const rmap_addr_t *address, chart_t **rx_out, chart_t **tx_out) {
    (void) address;
    *rx_out = NULL;
    *tx_out = NULL;

    tlm_async_init(&telemetry);
}

static void clock_start_main(void *opaque) {
    (void) opaque;

    // no adjustment needed on FreeRTOS.
    tlm_clock_calibrated(&telemetry, 0);

    // nothing left to do.
    task_suspend();
}
TASK_REGISTER(clock_start_task, "clock-start", PRIORITY_INIT, clock_start_main, NULL, NOT_RESTARTABLE);
