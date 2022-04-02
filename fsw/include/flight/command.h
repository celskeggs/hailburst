#ifndef FSW_CMD_H
#define FSW_CMD_H

#include <stddef.h>
#include <stdint.h>

#include <flight/spacecraft.h>

enum {
    COMMAND_REPLICAS = 1,
    COMMAND_REPLICA_ID = 0,

    // at most one command can be processed per epoch, and the worst case is PING, which produces three telemetry
    // messages: command received, pong, command completed
    COMMAND_MAX_TELEM_PER_EPOCH = 3,
};

typedef struct {
    comm_dec_t     *decoder;
    tlm_endpoint_t *telemetry;
} cmd_system_t;

void command_execution_clip(cmd_system_t *cs);

// may only be used once
macro_define(COMMAND_REGISTER, c_ident, c_uplink_pipe) {
    COMM_DEC_REGISTER(symbol_join(c_ident, decoder), c_uplink_pipe, COMMAND_REPLICA_ID);
    TELEMETRY_ASYNC_REGISTER(symbol_join(c_ident, telemetry), COMMAND_REPLICAS, COMMAND_MAX_TELEM_PER_EPOCH);
    cmd_system_t c_ident = {
        .decoder = &symbol_join(c_ident, decoder),
        .telemetry = &symbol_join(c_ident, telemetry),
    };
    CLIP_REGISTER(symbol_join(c_ident, clip), command_execution_clip, &c_ident)
}

macro_define(COMMAND_SCHEDULE, c_ident) {
    CLIP_SCHEDULE(symbol_join(c_ident, clip), 100)
}

macro_define(COMMAND_TELEMETRY, c_ident) {
    &symbol_join(c_ident, telemetry),
}

#endif /* FSW_CMD_H */
