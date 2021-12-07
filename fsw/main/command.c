#include <endian.h>
#include <inttypes.h>
#include <stdbool.h>

#include <fsw/cmd.h>
#include <fsw/debug.h>
#include <fsw/spacecraft.h>
#include <fsw/tlm.h>

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

static uint8_t zero[8] = {0};

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
    cmd_status_t (*cmd)(spacecraft_t *sc, tlm_async_endpoint_t *telemetry, cmd_parser_t *p);
} cmd_t;

static cmd_status_t cmd_ping(spacecraft_t *sc, tlm_async_endpoint_t *telemetry, cmd_parser_t *p) {
    (void) sc;
    // parse
    uint32_t ping_id = cmd_parse_u32(p);
    if (!cmd_parser_wrapup(p)) {
        return CMD_STATUS_UNRECOGNIZED;
    }
    // execute
    tlm_pong(telemetry, ping_id);
    return CMD_STATUS_OK;
}

static cmd_status_t cmd_mag_set_pwr_state(spacecraft_t *sc, tlm_async_endpoint_t *telemetry, cmd_parser_t *p) {
    (void) telemetry;
    // parse
    bool pwr_state = cmd_parse_bool(p);
    if (!cmd_parser_wrapup(p)) {
        return CMD_STATUS_UNRECOGNIZED;
    }
    // execute
    magnetometer_set_powered(&sc->mag, pwr_state);
    return CMD_STATUS_OK;
}

static cmd_t commands[] = {
    { .id = PING_CID,              .cmd = cmd_ping },
    { .id = MAG_SET_PWR_STATE_CID, .cmd = cmd_mag_set_pwr_state },
};

static cmd_status_t cmd_execute(spacecraft_t *sc, tlm_async_endpoint_t *telemetry,
                                uint32_t cid, const uint8_t *args, size_t args_len) {
    cmd_parser_t parser = {
        .bytes_ptr = args,
        .bytes_remaining = args_len,
        .parse_ok = true,
    };
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        if (commands[i].id == cid) {
            return commands[i].cmd(sc, telemetry, &parser);
        }
    }
    return CMD_STATUS_UNRECOGNIZED;
}

void cmd_mainloop(spacecraft_t *sc) {
    comm_packet_t packet;
    cmd_status_t status;
    tlm_async_endpoint_t telemetry;

    tlm_async_init(&telemetry);

    for (;;) {
        // wait for and decode next command
        comm_dec_decode(&sc->comm_decoder, &packet);
        // report reception
        tlm_cmd_received(&telemetry, packet.timestamp_ns, packet.cmd_tlm_id);
        // execute command
        status = cmd_execute(sc, &telemetry, packet.cmd_tlm_id, packet.data_bytes, packet.data_len);
        // report completion
        if (status == CMD_STATUS_UNRECOGNIZED) {
            tlm_cmd_not_recognized(&telemetry, packet.timestamp_ns, packet.cmd_tlm_id, packet.data_len);
        } else {
            assert(status == CMD_STATUS_OK || status == CMD_STATUS_FAIL);
            tlm_cmd_completed(&telemetry, packet.timestamp_ns, packet.cmd_tlm_id, status == CMD_STATUS_OK);
        }
    }
}
