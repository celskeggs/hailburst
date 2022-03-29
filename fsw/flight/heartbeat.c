#include <hal/watchdog.h>
#include <flight/clock_cal.h>
#include <flight/telemetry.h>

TELEMETRY_ASYNC_REGISTER(heartbeat_telemetry);

void heartbeat_mainloop(void) {
    clock_wait_for_calibration();

    // beat every 120 milliseconds (requirement is 150 milliseconds, so this is plenty fast)
    for (;;) {
        tlm_heartbeat(&heartbeat_telemetry);
        watchdog_ok(WATCHDOG_ASPECT_HEARTBEAT);

        task_delay(120000000);
    }
}

TASK_REGISTER(heartbeat_task, heartbeat_mainloop, NULL, RESTARTABLE);

CLOCK_DEPEND_ON_CALIBRATION(heartbeat_task);
