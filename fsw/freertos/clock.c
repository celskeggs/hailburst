#include <fsw/clock.h>
#include <fsw/clock_init.h>
#include <fsw/telemetry.h>

TELEMETRY_ASYNC_REGISTER(clock_telemetry);

static void clock_start(void) {
    // no adjustment needed on FreeRTOS.
    tlm_clock_calibrated(&clock_telemetry, 0);
}
PROGRAM_INIT(STAGE_CRAFT, clock_start);
