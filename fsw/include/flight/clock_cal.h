#ifndef FSW_CLOCK_INIT_H
#define FSW_CLOCK_INIT_H

#include <bus/rmap.h>
#include <bus/switch.h>
#include <flight/telemetry.h>

#define CLOCK_REPLICAS 1
#define CLOCK_REPLICA_ID 0

enum clock_state {
    CLOCK_IDLE,
    CLOCK_READ_MAGIC_NUMBER,
    CLOCK_READ_CURRENT_TIME,
    CLOCK_CALIBRATED,
};

typedef struct {
    enum clock_state state;
    rmap_replica_t  *rmap;
    tlm_endpoint_t  *telem;
} clock_device_t;

void clock_start_clip(clock_device_t *clock);
void clock_voter_clip(void);

macro_define(CLOCK_REGISTER, c_ident, c_address, c_switch_in, c_switch_out, c_switch_port) {
    RMAP_ON_SWITCHES(symbol_join(c_ident, rmap), CLOCK_REPLICAS, c_switch_in, c_switch_out, c_switch_port, c_address,
                     sizeof(uint64_t), 0);
    TELEMETRY_ASYNC_REGISTER(symbol_join(c_ident, telemetry), 1, 1);
    clock_device_t c_ident = {
        .rmap = RMAP_REPLICA_REF(symbol_join(c_ident, rmap), CLOCK_REPLICA_ID),
        .state = CLOCK_IDLE,
        .telem = &symbol_join(c_ident, telemetry),
    };
    CLIP_REGISTER(symbol_join(c_ident, clip), clock_start_clip, &c_ident);
    CLIP_REGISTER(symbol_join(c_ident, voter), clock_voter_clip, NULL)
}

macro_define(CLOCK_SCHEDULE, c_ident) {
    CLIP_SCHEDULE(symbol_join(c_ident, clip), 100)
    CLIP_SCHEDULE(symbol_join(c_ident, voter), 10)
}

macro_define(CLOCK_TELEMETRY, c_ident) {
    &symbol_join(c_ident, telemetry),
}

// one RMAP channel
#define CLOCK_MAX_IO_FLOW       RMAP_MAX_IO_FLOW

// largest packet size that the switch needs to be able to route
#define CLOCK_MAX_IO_PACKET                                                                                           \
    RMAP_MAX_IO_PACKET(sizeof(uint64_t), 0)

#endif /* FSW_CLOCK_INIT_H */
