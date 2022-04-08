#ifndef FSW_MAGNETOMETER_H
#define FSW_MAGNETOMETER_H

#include <stdbool.h>

#include <hal/thread.h>
#include <synch/circular.h>
#include <bus/rmap.h>
#include <bus/switch.h>
#include <flight/command.h>
#include <flight/telemetry.h>

#define MAGNETOMETER_REPLICAS 1

enum {
    MAGNETOMETER_MAX_READINGS = 100,
};

enum magnetometer_state {
    MS_INACTIVE = 0,
    MS_ACTIVATING,
    MS_ACTIVE,
    MS_LATCHING_ON,
    MS_LATCHED_ON,
    MS_TAKING_READING,
    MS_DEACTIVATING,
};

typedef const struct {
    struct magnetometer_replica_mut {
        // synchronization
        bool should_be_powered;

        // state saved between query clip invocations
        enum magnetometer_state state;
        local_time_t next_reading_time;
        local_time_t actual_reading_time;
        local_time_t check_latch_time;

        // state saved between telemetry clip invocations
        local_time_t last_telem_time;
    } *mut;

    uint8_t replica_id;

    // spacecraft bus connection
    rmap_replica_t *endpoint;

    // telemetry buffer
    circ_buf_t *readings;

    // telemetry and command endpoints
    tlm_endpoint_t *telemetry_async;
    tlm_endpoint_t *telemetry_sync;
    cmd_endpoint_t *command_endpoint;
} magnetometer_replica_t;

void magnetometer_query_clip(magnetometer_replica_t *mag);
void magnetometer_telem_clip(magnetometer_replica_t *mag);

macro_define(MAGNETOMETER_REGISTER, m_ident, m_address, m_switch_in, m_switch_out, m_switch_port) {
    TELEMETRY_ASYNC_REGISTER(symbol_join(m_ident, telemetry_async), MAGNETOMETER_REPLICAS, 2);
    TELEMETRY_SYNC_REGISTER(symbol_join(m_ident, telemetry_sync), MAGNETOMETER_REPLICAS, 1);
    COMMAND_ENDPOINT(symbol_join(m_ident, command), MAG_SET_PWR_STATE_CID, MAGNETOMETER_REPLICAS);
    RMAP_ON_SWITCHES(symbol_join(m_ident, endpoint), MAGNETOMETER_REPLICAS, m_switch_in, m_switch_out,
                     m_switch_port, m_address, 8, 4);
    static_repeat(MAGNETOMETER_REPLICAS, m_replica_id) {
        CIRC_BUF_REGISTER(symbol_join(m_ident, readings, m_replica_id),
                          sizeof(tlm_mag_reading_t), MAGNETOMETER_MAX_READINGS);
        struct magnetometer_replica_mut symbol_join(m_ident, mutable, m_replica_id) = {
            .should_be_powered = false,
            .state = MS_INACTIVE,
            .next_reading_time = 0,
            .actual_reading_time = 0,
            .check_latch_time = 0,
            .last_telem_time = 0,
        };
        magnetometer_replica_t symbol_join(m_ident, replica, m_replica_id) = {
            .mut = &symbol_join(m_ident, mutable, m_replica_id),
            .replica_id = m_replica_id,
            .endpoint = RMAP_REPLICA_REF(symbol_join(m_ident, endpoint), m_replica_id),
            .readings = &symbol_join(m_ident, readings, m_replica_id),
            .telemetry_async = &symbol_join(m_ident, telemetry_async),
            .telemetry_sync = &symbol_join(m_ident, telemetry_sync),
            .command_endpoint = &symbol_join(m_ident, command),
        };
        CLIP_REGISTER(symbol_join(m_ident, query, m_replica_id),
                      magnetometer_query_clip, &symbol_join(m_ident, replica, m_replica_id));
        CLIP_REGISTER(symbol_join(m_ident, telem, m_replica_id),
                      magnetometer_telem_clip, &symbol_join(m_ident, replica, m_replica_id));
    }
}

// one RMAP channel
#define MAGNETOMETER_MAX_IO_FLOW       RMAP_MAX_IO_FLOW

// largest packet size that the switch needs to be able to route
#define MAGNETOMETER_MAX_IO_PACKET     RMAP_MAX_IO_PACKET(8, 4)

macro_define(MAGNETOMETER_SCHEDULE, m_ident) {
    static_repeat(MAGNETOMETER_REPLICAS, m_replica_id) {
        CLIP_SCHEDULE(symbol_join(m_ident, query, m_replica_id), 20)
        CLIP_SCHEDULE(symbol_join(m_ident, telem, m_replica_id), 100)
    }
}

macro_define(MAGNETOMETER_TELEMETRY, m_ident) {
    TELEMETRY_ENDPOINT_REF(symbol_join(m_ident, telemetry_async))
    TELEMETRY_ENDPOINT_REF(symbol_join(m_ident, telemetry_sync))
}

macro_define(MAGNETOMETER_COMMAND, m_ident) {
    &symbol_join(m_ident, command),
}

#endif /* FSW_MAGNETOMETER_H */
