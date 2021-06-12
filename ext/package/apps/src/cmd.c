#include <arpa/inet.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include "cmd.h"
#include "tlm.h"

enum {
	PING_CID              = 0x01000001,
	MAG_SET_PWR_STATE_CID = 0x02000001,
};

static bool cmd_ping(uint32_t ping_id) {
    tlm_pong(ping_id);
    return true;
}

static bool cmd_mag_set_pwr_state(bool pwr_state) {
    printf("Not implemented: MAG_SET_PWR_STATE (%d)\n", pwr_state);
    tlm_mag_pwr_state_changed(pwr_state);
    return false;
}

void cmd_execute(uint32_t cid, uint64_t timestamp_ns, uint8_t *args, size_t args_len) {
    bool success;
    tlm_cmd_received(timestamp_ns, cid);
    if (cid == PING_CID && args_len == 4) {
        success = cmd_ping(ntohl(*(uint32_t*) args));
    } else if (cid == MAG_SET_PWR_STATE_CID && args_len == 1 && args[0] <= 1) {
        success = cmd_mag_set_pwr_state(args[0] != 0);
    } else {
        tlm_cmd_not_recognized(timestamp_ns, cid, args_len);
        return;
    }
    tlm_cmd_completed(timestamp_ns, cid, success);
}
