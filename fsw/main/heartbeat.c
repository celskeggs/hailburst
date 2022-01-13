#include <hal/watchdog.h>
#include <fsw/telemetry.h>

TELEMETRY_ASYNC_REGISTER(heartbeat_telemetry);

static void heartbeat_mainloop(void) {
    // beat every 120 milliseconds (requirement is 150 milliseconds, so this is plenty fast)
    for (;;) {
        tlm_heartbeat(&heartbeat_telemetry);
        watchdog_ok(WATCHDOG_ASPECT_HEARTBEAT);

        task_delay(120000000);
    }
}

TASK_REGISTER(heartbeat_task, "heartbeat_loop", PRIORITY_WORKERS, heartbeat_mainloop, NULL, RESTARTABLE);
