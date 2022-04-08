#ifndef FSW_HEARTBEAT_H
#define FSW_HEARTBEAT_H

#include <hal/thread.h>
#include <flight/clock_cal.h>

#define HEARTBEAT_REPLICAS 3

typedef const struct {
    struct heartbeat_mut {
        local_time_t    last_heartbeat_time;
    } *mut;
    tlm_endpoint_t *telemetry;
    watchdog_aspect_t *aspect;
    uint8_t replica_id;
} heartbeat_replica_t;

void heartbeat_main_clip(heartbeat_replica_t *h);

macro_define(HEARTBEAT_REGISTER, h_ident) {
    TELEMETRY_ASYNC_REGISTER(symbol_join(h_ident, telemetry), HEARTBEAT_REPLICAS, 1);
    WATCHDOG_ASPECT(symbol_join(h_ident, aspect), 1 * CLOCK_NS_PER_SEC, HEARTBEAT_REPLICAS);
    static_repeat(HEARTBEAT_REPLICAS, h_replica_id) {
        struct heartbeat_mut symbol_join(h_ident, replica, h_replica_id, mut) = {
            .last_heartbeat_time = 0,
        };
        heartbeat_replica_t symbol_join(h_ident, replica, h_replica_id) = {
            .mut = &symbol_join(h_ident, replica, h_replica_id, mut),
            .telemetry = &symbol_join(h_ident, telemetry),
            .aspect = &symbol_join(h_ident, aspect),
            .replica_id = h_replica_id,
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
