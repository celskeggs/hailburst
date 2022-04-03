#include <endian.h>

#include <hal/debug.h>
#include <flight/pingback.h>
#include <flight/telemetry.h>

void pingback_clip(pingback_t *p) {
    assert(p != NULL);

    tlm_txn_t telem;
    telemetry_prepare(&telem, p->telemetry, PINGBACK_REPLICA_ID);

    size_t command_length = 0;
    uint32_t *command_data = command_receive(p->command, PINGBACK_REPLICA_ID, &command_length);
    if (command_data != NULL) {
        if (command_length == sizeof(uint32_t)) {
            uint32_t ping_id = be32toh(command_data[0]);
            tlm_pong(&telem, ping_id);
            command_reply(p->command, &telem, CMD_STATUS_OK);
        } else {
            // wrong length
            command_reply(p->command, &telem, CMD_STATUS_UNRECOGNIZED);
        }
    }

    telemetry_commit(&telem);
}
