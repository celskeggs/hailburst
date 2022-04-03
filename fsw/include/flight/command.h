#ifndef FSW_FLIGHT_COMMAND_H
#define FSW_FLIGHT_COMMAND_H

#include <stdint.h>

#include <hal/time.h>
#include <synch/duct.h>
#include <flight/comm.h>
#include <flight/telemetry.h>

enum {
    COMMAND_REPLICAS = 1,
    COMMAND_REPLICA_ID = 0,

    // none of the currently-defined commands exceed this length
    COMMAND_MAX_PARAM_LENGTH = 4,

    // at most one command can be processed per epoch. the worst case is an unrecognized command ID, which will lead to
    // two telemetry messages.
    COMMAND_MAX_TELEM_PER_EPOCH = 2,
};

typedef enum {
    CMD_STATUS_OK = 0,            // command succeeded
    CMD_STATUS_FAIL = 1,          // command failed
    CMD_STATUS_UNRECOGNIZED = 2,  // command not valid
} cmd_status_t;

typedef enum {
    PING_CID              = 0x01000001,
    MAG_SET_PWR_STATE_CID = 0x02000001,
} cmd_id_t;

typedef struct {
    cmd_id_t       cid;
    duct_t        *duct;
    // the reason that we're squishing the mission time into the duct message, rather than passing it using duct's
    // out-of-band API, is for three reasons:
    //    1. ducts won't let us transmit 0-length messages, and some commands are 0-length, so some sort of padding
    //       must be added in at least that case.
    //    2. this is a mission clock timestamp, rather than a local clock timestamp, and the duct API is defined to use
    //       local clock timestamps. not really important, but it's nice to avoid type confusion.
    //    3. timestamp passing might be removed from duct in the future... it's a little bit of an auxiliary feature!
    //       (it might be replaced by a "minimum message length" setting instead, to simplify receiver code.)
    struct cmd_duct_msg {
        mission_time_t timestamp;
        uint8_t        data[COMMAND_MAX_PARAM_LENGTH];
    } __attribute__((packed)) last_received;
    size_t                    last_data_length;
} cmd_endpoint_t;

typedef const struct {
    comm_dec_t      *decoder;
    cmd_endpoint_t **endpoints;
    size_t           num_endpoints;
    tlm_endpoint_t  *telemetry;
} cmd_system_t;

void command_execution_clip(cmd_system_t *cs);

macro_define(COMMAND_SYSTEM_REGISTER, c_ident, c_uplink_pipe, c_commands) {
    COMM_DEC_REGISTER(symbol_join(c_ident, decoder), c_uplink_pipe, COMMAND_REPLICA_ID);
    TELEMETRY_ASYNC_REGISTER(symbol_join(c_ident, telemetry), COMMAND_REPLICAS, COMMAND_MAX_TELEM_PER_EPOCH);
    cmd_endpoint_t *symbol_join(c_ident, endpoints)[] = c_commands;
    cmd_system_t c_ident = {
        .decoder = &symbol_join(c_ident, decoder),
        .endpoints = symbol_join(c_ident, endpoints),
        .num_endpoints = sizeof(symbol_join(c_ident, endpoints)) / sizeof(symbol_join(c_ident, endpoints)[0]),
        .telemetry = &symbol_join(c_ident, telemetry),
    };
    CLIP_REGISTER(symbol_join(c_ident, clip), command_execution_clip, &c_ident)
}

macro_define(COMMAND_ENDPOINT, e_ident, e_command_id, e_receiver_replicas) {
    DUCT_REGISTER(symbol_join(e_ident, duct), COMMAND_REPLICAS, e_receiver_replicas,
                  1, sizeof(struct cmd_duct_msg), DUCT_SENDER_FIRST);
    cmd_endpoint_t e_ident = {
        .cid = (e_command_id),
        .duct = &symbol_join(e_ident, duct),
    }
}

macro_define(COMMAND_SCHEDULE, c_ident) {
    CLIP_SCHEDULE(symbol_join(c_ident, clip), 100)
}

macro_define(COMMAND_TELEMETRY, c_ident) {
    &symbol_join(c_ident, telemetry),
}

// must be called every epoch. returns NULL if no command is available.
// if a command is available, a pointer to it will be returned.
void *command_receive(cmd_endpoint_t *ce, uint8_t replica_id, size_t *size_out);

// to indicate completion (OK, FAIL, or UNRECOGNIZED), call this function. it will transmit one telemetry message.
void command_reply(cmd_endpoint_t *ce, tlm_txn_t *telem, cmd_status_t status);

#endif /* FSW_FLIGHT_COMMAND_H */
