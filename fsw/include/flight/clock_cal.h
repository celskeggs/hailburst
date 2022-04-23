#ifndef FSW_CLOCK_INIT_H
#define FSW_CLOCK_INIT_H

#include <synch/notepad.h>
#include <bus/rmap.h>
#include <bus/switch.h>
#include <flight/telemetry.h>

enum clock_state {
    CLOCK_IDLE,
    CLOCK_READ_MAGIC_NUMBER,
    CLOCK_READ_CURRENT_TIME,
    CLOCK_CALIBRATED,
};

struct clock_replica_note {
    enum clock_state state;
    rmap_synch_t     rmap_synch;
};

typedef const struct {
    uint8_t replica_id;
    notepad_ref_t  *synch;
    rmap_replica_t *rmap;
    tlm_endpoint_t *telem;
} clock_replica_t;

void clock_start_clip(clock_replica_t *cr);
void clock_voter_clip(void);

macro_define(CLOCK_REGISTER, c_ident, c_address, c_switch_in, c_switch_out, c_switch_port) {
    RMAP_ON_SWITCHES(symbol_join(c_ident, rmap), CLOCK_REPLICAS, c_switch_in, c_switch_out, c_switch_port, c_address,
                     sizeof(uint64_t), 0);
    TELEMETRY_ASYNC_REGISTER(symbol_join(c_ident, telemetry), CLOCK_REPLICAS, 1);
    NOTEPAD_REGISTER(symbol_join(c_ident, notepad), CLOCK_REPLICAS, 0, sizeof(struct clock_replica_note));
    static_repeat(CLOCK_REPLICAS, c_replica_id) {
        clock_replica_t symbol_join(c_ident, replica, c_replica_id) = {
            .replica_id = c_replica_id,
            .synch = NOTEPAD_REPLICA_REF(symbol_join(c_ident, notepad), c_replica_id),
            .rmap = RMAP_REPLICA_REF(symbol_join(c_ident, rmap), c_replica_id),
            .telem = &symbol_join(c_ident, telemetry),
        };
        CLIP_REGISTER(symbol_join(c_ident, clip, c_replica_id),
                      clock_start_clip, &symbol_join(c_ident, replica, c_replica_id));
    }
    CLIP_REGISTER(symbol_join(c_ident, voter), clock_voter_clip, NULL)
}

macro_define(CLOCK_SCHEDULE, c_ident) {
    static_repeat(CLOCK_REPLICAS, c_replica_id) {
        CLIP_SCHEDULE(symbol_join(c_ident, clip, c_replica_id), 10)
    }
    CLIP_SCHEDULE(symbol_join(c_ident, voter), 10)
}

macro_define(CLOCK_TELEMETRY, c_ident) {
    TELEMETRY_ENDPOINT_REF(symbol_join(c_ident, telemetry))
}

// one RMAP channel
#define CLOCK_MAX_IO_FLOW       RMAP_MAX_IO_FLOW

// largest packet size that the switch needs to be able to route
#define CLOCK_MAX_IO_PACKET                                                                                           \
    RMAP_MAX_IO_PACKET(sizeof(uint64_t), 0)

#endif /* FSW_CLOCK_INIT_H */
