#ifndef FSW_CMD_H
#define FSW_CMD_H

#include <stddef.h>
#include <stdint.h>

#include <flight/spacecraft.h>

void command_execution_clip(comm_dec_t *decoder);

enum {
    COMMAND_REPLICA_ID = 0,
};

// may only be used once
macro_define(COMMAND_REGISTER, c_ident, c_uplink_pipe) {
    COMM_DEC_REGISTER(symbol_join(c_ident, decoder), c_uplink_pipe, COMMAND_REPLICA_ID);
    CLIP_REGISTER(symbol_join(c_ident, task), command_execution_clip, &symbol_join(c_ident, decoder))
}

macro_define(COMMAND_SCHEDULE, c_ident) {
    CLIP_SCHEDULE(symbol_join(c_ident, task), 100)
}

#endif /* FSW_CMD_H */
