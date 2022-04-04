#ifndef FSW_TLM_H
#define FSW_TLM_H

#include <stdbool.h>
#include <stdint.h>

#include <hal/clip.h>
#include <hal/init.h>
#include <hal/watchdog.h>
#include <synch/circular.h>
#include <synch/duct.h>
#include <synch/pipe.h>
#include <flight/comm.h>

#define TELEMETRY_REPLICAS 1

enum {
    TELEMETRY_REPLICA_ID = 0,

    TLM_MAX_ASYNC_SIZE = 16,
    TLM_MAX_SYNC_SIZE  = 64 * 1024,
};

// should fit on the stack
typedef struct {
    uint32_t telemetry_id;
    uint8_t  data_bytes[TLM_MAX_ASYNC_SIZE];
} tlm_async_t;

// probably doesn't fit on the stack
typedef struct {
    uint32_t telemetry_id;
    uint8_t  data_bytes[TLM_MAX_SYNC_SIZE];
} tlm_sync_t;

// slot for a tlm_sync_t and a length field.
typedef struct {
    size_t       data_length;
    local_time_t timestamp;
    tlm_sync_t   sync_data;
} tlm_sync_slot_t;

typedef struct {
    mission_time_t reading_time;
    int16_t        mag_x;
    int16_t        mag_y;
    int16_t        mag_z;
} tlm_mag_reading_t;

typedef const struct {
    bool is_synchronous;
    union {
        duct_t *async_duct;
        struct {
            pipe_t     *sync_pipe;
            tlm_sync_t *sender_scratch;
            circ_buf_t *receiver_scratch[TELEMETRY_REPLICAS];
        };
    };
} tlm_endpoint_t;

typedef struct {
    tlm_endpoint_t *ep;
    uint8_t replica_id;
    union {
        duct_txn_t async_txn;
        pipe_txn_t sync_txn;
    };
} tlm_txn_t;

typedef const struct {
    struct tlm_system_mut {
        uint32_t async_dropped;
    } *mut;
    comm_enc_t             *comm_encoder;
    tlm_endpoint_t * const *endpoints;
    size_t                  num_endpoints;
    watchdog_aspect_t      *aspect;
} tlm_system_t;

void telemetry_pump(tlm_system_t *ts);

macro_define(TELEMETRY_SYSTEM_REGISTER, t_ident, t_pipe, t_components) {
    COMM_ENC_REGISTER(symbol_join(t_ident, encoder), t_pipe, TELEMETRY_REPLICA_ID);
    tlm_endpoint_t * const symbol_join(t_ident, endpoints)[] = t_components;
    struct tlm_system_mut symbol_join(t_ident, mutable) = {
        .async_dropped = 0,
    };
    WATCHDOG_ASPECT(symbol_join(t_ident, aspect), TELEMETRY_REPLICAS);
    tlm_system_t t_ident = {
        .mut = &symbol_join(t_ident, mutable),
        .comm_encoder = &symbol_join(t_ident, encoder),
        .endpoints = symbol_join(t_ident, endpoints),
        .num_endpoints = sizeof(symbol_join(t_ident, endpoints)) / sizeof(*symbol_join(t_ident, endpoints)),
        .aspect = &symbol_join(t_ident, aspect),
    };
    CLIP_REGISTER(symbol_join(t_ident, clip), telemetry_pump, &t_ident)
}

macro_define(TELEMETRY_SCHEDULE, t_ident) {
    CLIP_SCHEDULE(symbol_join(t_ident, clip), 100)
}

macro_define(TELEMETRY_WATCH, t_ident) {
    &symbol_join(t_ident, aspect),
}

macro_define(TELEMETRY_ASYNC_REGISTER, e_ident, e_replicas, e_max_flow) {
    DUCT_REGISTER(symbol_join(e_ident, duct), e_replicas, TELEMETRY_REPLICAS, e_max_flow, sizeof(tlm_async_t),
                  DUCT_SENDER_FIRST);
    tlm_endpoint_t e_ident = {
        .is_synchronous = false,
        .async_duct = &symbol_join(e_ident, duct),
    }
}

macro_define(TELEMETRY_SYNC_REGISTER, e_ident, e_replicas, e_max_flow) {
    PIPE_REGISTER(symbol_join(e_ident, pipe), e_replicas, TELEMETRY_REPLICAS, e_max_flow, sizeof(tlm_sync_t),
                  PIPE_SENDER_FIRST);
    tlm_sync_t symbol_join(e_ident, sender_scratch)[e_replicas];
    static_repeat(TELEMETRY_REPLICAS, replica_id) {
        CIRC_BUF_REGISTER(symbol_join(e_ident, receiver_scratch, replica_id), sizeof(tlm_sync_slot_t), e_max_flow);
    }
    tlm_endpoint_t e_ident = {
        .is_synchronous = true,
        .sync_pipe = &symbol_join(e_ident, pipe),
        .sender_scratch = symbol_join(e_ident, sender_scratch),
        .receiver_scratch = {
            static_repeat(TELEMETRY_REPLICAS, replica_id) {
                &symbol_join(e_ident, receiver_scratch, replica_id),
            }
        },
    }
}

void telemetry_prepare(tlm_txn_t *txn, tlm_endpoint_t *ep, uint8_t sender_id);
bool telemetry_can_send(tlm_txn_t *txn);
void telemetry_commit(tlm_txn_t *txn);

// actual telemetry calls
void tlm_cmd_received(tlm_txn_t *txn, uint64_t original_timestamp, uint32_t original_command_id);
void tlm_cmd_completed(tlm_txn_t *txn, uint64_t original_timestamp, uint32_t original_command_id, bool success);
void tlm_cmd_not_recognized(tlm_txn_t *txn, uint64_t original_timestamp, uint32_t original_command_id,
                            uint32_t length);
void tlm_pong(tlm_txn_t *txn, uint32_t ping_id);
void tlm_clock_calibrated(tlm_txn_t *txn, int64_t adjustment);
void tlm_heartbeat(tlm_txn_t *txn);
void tlm_mag_pwr_state_changed(tlm_txn_t *txn, bool power_state);
void tlm_mag_readings_map(tlm_txn_t *txn, size_t *fetch_count,
                          void (*fetch)(void *param, size_t index, tlm_mag_reading_t *out), void *param);

#endif /* FSW_TLM_H */
