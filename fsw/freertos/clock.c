#include <hal/thread.h>
#include <fsw/clock.h>
#include <fsw/clock_init.h>
#include <fsw/telemetry.h>

TELEMETRY_ASYNC_REGISTER(clock_telemetry);

static void clock_start_main(void) {
    // no adjustment needed on FreeRTOS.
    tlm_clock_calibrated(&clock_telemetry, 0);

    // nothing left to do.
    task_suspend();
}
TASK_REGISTER(clock_start_task, "clock-start", PRIORITY_INIT, clock_start_main, NULL, NOT_RESTARTABLE);
