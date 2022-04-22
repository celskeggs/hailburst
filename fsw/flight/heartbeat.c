#include <hal/watchdog.h>
#include <flight/heartbeat.h>
#include <flight/telemetry.h>

enum {
    // beat every 120 milliseconds (requirement is 150 milliseconds, so this is plenty fast)
    HEARTBEAT_PERIOD = 120 * CLOCK_NS_PER_MS,
};

void heartbeat_main_clip(heartbeat_replica_t *h) {
    local_time_t now = timer_epoch_ns();

    bool valid = false;
    struct heartbeat_note *mut_synch = notepad_feedforward(h->mut_synch, &valid);
    if (!valid || mut_synch->last_heartbeat_time > now) {
        mut_synch->last_heartbeat_time = now - HEARTBEAT_PERIOD;
    }

    tlm_txn_t telem;
    telemetry_prepare(&telem, h->telemetry, h->replica_id);

    bool watchdog_ok = false;

    if (clock_is_calibrated() && now >= mut_synch->last_heartbeat_time + HEARTBEAT_PERIOD) {
        tlm_heartbeat(&telem);

        watchdog_ok = true;

        mut_synch->last_heartbeat_time = now;
    }

    watchdog_indicate(h->aspect, h->replica_id, watchdog_ok);

    telemetry_commit(&telem);
}
