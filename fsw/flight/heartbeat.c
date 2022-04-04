#include <hal/watchdog.h>
#include <flight/heartbeat.h>
#include <flight/telemetry.h>

enum {
    // beat every 120 milliseconds (requirement is 150 milliseconds, so this is plenty fast)
    HEARTBEAT_PERIOD = 120 * CLOCK_NS_PER_MS,
};

void heartbeat_main_clip(heartbeat_t *h) {
    if (clip_is_restart()) {
        // heartbeat immediately on restart
        h->mut->last_heartbeat_time = timer_now_ns() - HEARTBEAT_PERIOD;
    }

    tlm_txn_t telem;
    telemetry_prepare(&telem, h->telemetry, HEARTBEAT_REPLICA_ID);

    bool watchdog_ok = false;

    if (clock_is_calibrated() && timer_now_ns() >= h->mut->last_heartbeat_time + HEARTBEAT_PERIOD) {
        tlm_heartbeat(&telem);

        watchdog_ok = true;

        h->mut->last_heartbeat_time = timer_now_ns();
    }

    watchdog_indicate(h->aspect, HEARTBEAT_REPLICA_ID, watchdog_ok);

    telemetry_commit(&telem);
}
