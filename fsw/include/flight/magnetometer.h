#ifndef FSW_MAGNETOMETER_H
#define FSW_MAGNETOMETER_H

#include <stdbool.h>

#include <hal/thread.h>
#include <synch/circular.h>
#include <bus/rmap.h>
#include <bus/switch.h>
#include <flight/telemetry.h>

#define MAGNETOMETER_REPLICAS 1

enum {
    MAGNETOMETER_MAX_READINGS = 100,
    MAGNETOMETER_REPLICA_ID = 0,
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

typedef struct {
    rmap_t *endpoint;

    // synchronization
    bool should_be_powered;

    // telemetry buffer
    circ_buf_t *readings;

    // state saved between query clip invocations
    enum magnetometer_state state;
    local_time_t next_reading_time;
    local_time_t actual_reading_time;
    local_time_t check_latch_time;

    // state saved between telemetry clip invocations
    local_time_t last_telem_time;

    // telemetry output endpoint
    tlm_endpoint_t *telemetry_async;
    tlm_endpoint_t *telemetry_sync;
} magnetometer_t;

void magnetometer_drop_notification(void);
void magnetometer_query_clip(magnetometer_t *mag);
void magnetometer_telem_clip(magnetometer_t *mag);

macro_define(MAGNETOMETER_REGISTER, m_ident, m_address, m_switch_in, m_switch_out, m_switch_port) {
    CIRC_BUF_REGISTER(symbol_join(m_ident, readings), sizeof(tlm_mag_reading_t), MAGNETOMETER_MAX_READINGS);
    TELEMETRY_ASYNC_REGISTER(symbol_join(m_ident, telemetry_async), MAGNETOMETER_REPLICAS, 1);
    TELEMETRY_SYNC_REGISTER(symbol_join(m_ident, telemetry_sync), MAGNETOMETER_REPLICAS, 1);
    RMAP_ON_SWITCHES(symbol_join(m_ident, endpoint), "magnet", m_switch_in, m_switch_out, m_switch_port, m_address,
                     8, 4);
    magnetometer_t m_ident = {
        .endpoint = &symbol_join(m_ident, endpoint),
        .should_be_powered = false,
        .readings = &symbol_join(m_ident, readings),
        .state = MS_INACTIVE,
        .next_reading_time = 0,
        .actual_reading_time = 0,
        .check_latch_time = 0,
        .last_telem_time = 0,
        .telemetry_async = &symbol_join(m_ident, telemetry_async),
        .telemetry_sync = &symbol_join(m_ident, telemetry_sync),
    };
    CLIP_REGISTER(symbol_join(m_ident, query), magnetometer_query_clip, &m_ident);
    CLIP_REGISTER(symbol_join(m_ident, telem), magnetometer_telem_clip, &m_ident)
}

// one RMAP channel
#define MAGNETOMETER_MAX_IO_FLOW       RMAP_MAX_IO_FLOW

// largest packet size that the switch needs to be able to route
#define MAGNETOMETER_MAX_IO_PACKET     RMAP_MAX_IO_PACKET(8, 4)

macro_define(MAGNETOMETER_SCHEDULE, m_ident) {
    CLIP_SCHEDULE(symbol_join(m_ident, query), 20)
    CLIP_SCHEDULE(symbol_join(m_ident, telem), 100)
}

macro_define(MAGNETOMETER_TELEMETRY, m_ident) {
    &symbol_join(m_ident, telemetry_async),
    &symbol_join(m_ident, telemetry_sync),
}

void magnetometer_set_powered(magnetometer_t *mag, bool powered);

#endif /* FSW_MAGNETOMETER_H */
