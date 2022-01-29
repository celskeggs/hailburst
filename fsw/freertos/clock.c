#include <hal/clock.h>
#include <hal/clock_init.h>
#include <flight/telemetry.h>

TELEMETRY_ASYNC_REGISTER(clock_telemetry);

static void clock_start(void) {
    // no adjustment needed on FreeRTOS.
    tlm_clock_calibrated(&clock_telemetry, 0);
}
PROGRAM_INIT(STAGE_CRAFT, clock_start);
