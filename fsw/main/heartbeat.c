#include <unistd.h>

#include <hal/watchdog.h>
#include <fsw/debug.h>
#include <fsw/heartbeat.h>
#include <fsw/tlm.h>

static void heartbeat_mainloop(void *opaque) {
    assert(opaque != NULL);
    heartbeat_t *heart = (heartbeat_t *) opaque;

    // beat every 120 milliseconds (requirement is 150 milliseconds, so this is plenty fast)
    for (;;) {
        tlm_heartbeat(&heart->telemetry);
        watchdog_ok(WATCHDOG_ASPECT_HEARTBEAT);

        usleep(120 * 1000);
    }
}

void heartbeat_init(heartbeat_t *heart) {
    tlm_async_init(&heart->telemetry);
    thread_create(&heart->thread, "heartbeat_loop", PRIORITY_WORKERS, heartbeat_mainloop, heart, RESTARTABLE);
}
