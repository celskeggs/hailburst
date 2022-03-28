#include <endian.h>
#include <inttypes.h>
#include <stdbool.h>

#include <hal/debug.h>
#include <flight/command.h>
#include <flight/spacecraft.h>
#include <flight/telemetry.h>

enum {
    PING_CID              = 0x01000001,
    MAG_SET_PWR_STATE_CID = 0x02000001,
};

typedef enum {
    CMD_STATUS_OK = 0,            // command succeeded
    CMD_STATUS_FAIL = 1,          // command failed
    CMD_STATUS_UNRECOGNIZED = 2,  // command not valid
} cmd_status_t;

typedef struct {
    const uint8_t *bytes_ptr;
    ssize_t        bytes_remaining;
    bool           parse_ok;
} cmd_parser_t;

static const uint8_t zero[8] = {0};

static const uint8_t *cmd_parser_consume(cmd_parser_t *parser, ssize_t n_bytes) {
    assert(n_bytes > 0 && n_bytes <= (ssize_t) sizeof(zero));
    parser->bytes_remaining -= n_bytes;
    if (parser->bytes_remaining >= 0) {
        const uint8_t *ptr_out = parser->bytes_ptr;
        parser->bytes_ptr += n_bytes;
        return ptr_out;
    }
    return (uint8_t*) &zero;
}

static bool cmd_parser_wrapup(cmd_parser_t *parser) {
    return parser->bytes_remaining == 0 && parser->parse_ok;
}

static uint8_t cmd_parse_u8(cmd_parser_t *parser) {
    const uint8_t *ptr = cmd_parser_consume(parser, 1);
    return *ptr;
}

static uint32_t cmd_parse_u32(cmd_parser_t *parser) {
    const uint8_t *ptr = cmd_parser_consume(parser, 4);
    return (ptr[0] << 24) | (ptr[1] << 16) | (ptr[2] << 8) | ptr[3];
}

static bool cmd_parse_bool(cmd_parser_t *parser) {
    switch (cmd_parse_u8(parser)) {
    case 0:
        return false;
    case 1:
        return true;
    default:
        parser->parse_ok = false;
        return false;
    }
}

typedef struct {
    uint32_t     id;
    cmd_status_t (*cmd)(tlm_async_endpoint_t *telemetry, cmd_parser_t *p);
} cmd_t;

static cmd_status_t cmd_ping(tlm_async_endpoint_t *telemetry, cmd_parser_t *p) {
    // parse
    uint32_t ping_id = cmd_parse_u32(p);
    if (!cmd_parser_wrapup(p)) {
        return CMD_STATUS_UNRECOGNIZED;
    }
    // execute
    tlm_pong(telemetry, ping_id);
    return CMD_STATUS_OK;
}

static cmd_status_t cmd_mag_set_pwr_state(tlm_async_endpoint_t *telemetry, cmd_parser_t *p) {
    (void) telemetry;
    // parse
    bool pwr_state = cmd_parse_bool(p);
    if (!cmd_parser_wrapup(p)) {
        return CMD_STATUS_UNRECOGNIZED;
    }
    // execute
    magnetometer_set_powered(&sc_mag, pwr_state);
    return CMD_STATUS_OK;
}

static const cmd_t commands[] = {
    { .id = PING_CID,              .cmd = cmd_ping },
    { .id = MAG_SET_PWR_STATE_CID, .cmd = cmd_mag_set_pwr_state },
};

static cmd_status_t cmd_execute(tlm_async_endpoint_t *telemetry,
                                uint32_t cid, const uint8_t *args, size_t args_len) {
    cmd_parser_t parser = {
        .bytes_ptr = args,
        .bytes_remaining = args_len,
        .parse_ok = true,
    };
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        if (commands[i].id == cid) {
            return commands[i].cmd(telemetry, &parser);
        }
    }
    return CMD_STATUS_UNRECOGNIZED;
}

TELEMETRY_ASYNC_REGISTER(cmd_telemetry);

void command_execution_clip(comm_dec_t *decoder) {
    assert(decoder != NULL);
    comm_packet_t packet;
    cmd_status_t status;

    if (clip_is_restart()) {
        comm_dec_reset(decoder);
    }

    comm_dec_prepare(decoder);

    while (comm_dec_decode(decoder, &packet)) {
        // report reception
        tlm_cmd_received(&cmd_telemetry, packet.timestamp_ns, packet.cmd_tlm_id);
        // execute command
        status = cmd_execute(&cmd_telemetry, packet.cmd_tlm_id, packet.data_bytes, packet.data_len);
        // report completion
        if (status == CMD_STATUS_UNRECOGNIZED) {
            tlm_cmd_not_recognized(&cmd_telemetry, packet.timestamp_ns, packet.cmd_tlm_id, packet.data_len);
        } else {
            assert(status == CMD_STATUS_OK || status == CMD_STATUS_FAIL);
            tlm_cmd_completed(&cmd_telemetry, packet.timestamp_ns, packet.cmd_tlm_id, status == CMD_STATUS_OK);
        }
    }

    comm_dec_commit(decoder);
}
