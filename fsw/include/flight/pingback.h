#ifndef FSW_FLIGHT_PINGBACK_H
#define FSW_FLIGHT_PINGBACK_H

#include <hal/thread.h>
#include <flight/command.h>
#include <flight/telemetry.h>

#define PINGBACK_REPLICAS 1

enum {
    PINGBACK_REPLICA_ID = 0,
};

typedef const struct {
    tlm_endpoint_t *telemetry;
    cmd_endpoint_t *command;
} pingback_t;

void pingback_clip(pingback_t *p);

macro_define(PINGBACK_REGISTER, p_ident) {
    TELEMETRY_ASYNC_REGISTER(symbol_join(p_ident, telemetry), PINGBACK_REPLICAS, 2);
    COMMAND_ENDPOINT(symbol_join(p_ident, command), PING_CID, PINGBACK_REPLICAS);
    pingback_t p_ident = {
        .telemetry = &symbol_join(p_ident, telemetry),
        .command = &symbol_join(p_ident, command),
    };
    CLIP_REGISTER(symbol_join(p_ident, clip), pingback_clip, &p_ident)
}

macro_define(PINGBACK_SCHEDULE, p_ident) {
    CLIP_SCHEDULE(symbol_join(p_ident, clip), 10)
}

macro_define(PINGBACK_TELEMETRY, p_ident) {
    &symbol_join(p_ident, telemetry),
}

macro_define(PINGBACK_COMMAND, p_ident) {
    &symbol_join(p_ident, command),
}

#endif /* FSW_FLIGHT_PINGBACK_H */
