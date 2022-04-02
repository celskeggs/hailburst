#ifndef FSW_HEARTBEAT_H
#define FSW_HEARTBEAT_H

#include <hal/thread.h>
#include <flight/clock_cal.h>

enum {
    HEARTBEAT_REPLICAS = 1,
    HEARTBEAT_REPLICA_ID = 0,
};

typedef const struct {
    struct heartbeat_mut {
        local_time_t    last_heartbeat_time;
    } *mut;
    tlm_endpoint_t *telemetry;
} heartbeat_t;

void heartbeat_main_clip(heartbeat_t *h);

macro_define(HEARTBEAT_REGISTER, h_ident) {
    TELEMETRY_ASYNC_REGISTER(symbol_join(h_ident, telemetry), HEARTBEAT_REPLICAS, 1);
    struct heartbeat_mut symbol_join(h_ident, mut) = {
        .last_heartbeat_time = 0,
    };
    heartbeat_t h_ident = {
        .mut = &symbol_join(h_ident, mut),
        .telemetry = &symbol_join(h_ident, telemetry),
    };
    CLIP_REGISTER(symbol_join(h_ident, clip), heartbeat_main_clip, &h_ident)
}

macro_define(HEARTBEAT_SCHEDULE, h_ident) {
    CLIP_SCHEDULE(symbol_join(h_ident, clip), 10)
}

macro_define(HEARTBEAT_TELEMETRY, h_ident) {
    &symbol_join(h_ident, telemetry),
}

#endif /* FSW_HEARTBEAT_H */
