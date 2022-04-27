#ifndef FSW_HEARTBEAT_H
#define FSW_HEARTBEAT_H

#include <hal/thread.h>
#include <synch/notepad.h>
#include <flight/clock_cal.h>

// use default number of replicas
#define HEARTBEAT_REPLICAS CONFIG_APPLICATION_REPLICAS

struct heartbeat_note {
    local_time_t last_heartbeat_time;
};

typedef const struct {
    uint8_t            replica_id;
    notepad_ref_t     *mut_synch;
    tlm_endpoint_t    *telemetry;
    watchdog_aspect_t *aspect;
} heartbeat_replica_t;

void heartbeat_main_clip(heartbeat_replica_t *h);

macro_define(HEARTBEAT_REGISTER, h_ident) {
    TELEMETRY_ASYNC_REGISTER(symbol_join(h_ident, telemetry), HEARTBEAT_REPLICAS, 1);
    WATCHDOG_ASPECT(symbol_join(h_ident, aspect), 1 * CLOCK_NS_PER_SEC, HEARTBEAT_REPLICAS);
    NOTEPAD_REGISTER(symbol_join(h_ident, notepad), HEARTBEAT_REPLICAS, 0, sizeof(struct heartbeat_note));
    static_repeat(HEARTBEAT_REPLICAS, h_replica_id) {
        heartbeat_replica_t symbol_join(h_ident, replica, h_replica_id) = {
            .replica_id = h_replica_id,
            .mut_synch = NOTEPAD_REPLICA_REF(symbol_join(h_ident, notepad), h_replica_id),
            .telemetry = &symbol_join(h_ident, telemetry),
            .aspect = &symbol_join(h_ident, aspect),
        };
        CLIP_REGISTER(symbol_join(h_ident, clip, h_replica_id),
                      heartbeat_main_clip, &symbol_join(h_ident, replica, h_replica_id));
    }
}

macro_define(HEARTBEAT_SCHEDULE, h_ident) {
    static_repeat(HEARTBEAT_REPLICAS, h_replica_id) {
        CLIP_SCHEDULE(symbol_join(h_ident, clip, h_replica_id), 10)
    }
}

macro_define(HEARTBEAT_TELEMETRY, h_ident) {
    TELEMETRY_ENDPOINT_REF(symbol_join(h_ident, telemetry))
}

macro_define(HEARTBEAT_WATCH, h_ident) {
    &symbol_join(h_ident, aspect),
}

#endif /* FSW_HEARTBEAT_H */
