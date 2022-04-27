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

// use default number of replicas
#define TELEMETRY_REPLICAS CONFIG_APPLICATION_REPLICAS

enum {
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
        };
    };
} tlm_endpoint_t;

typedef const struct {
    bool is_synchronous;
    union {
        duct_t *async_duct;
        struct {
            pipe_t     *sync_pipe;
            circ_buf_t *receiver_scratch;
        };
    };
} tlm_registration_replica_t;

typedef const struct {
    tlm_registration_replica_t replicas[TELEMETRY_REPLICAS];
} tlm_registration_t;

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
    uint8_t                     replica_id;
    comm_enc_t                 *comm_encoder;
    tlm_registration_t * const *registrations;
    size_t                      num_registrations;
    watchdog_aspect_t          *aspect;
} tlm_replica_t;

void telemetry_pump(tlm_replica_t *ts);

macro_define(TELEMETRY_SYSTEM_REGISTER, t_ident, t_pipe, t_components) {
    WATCHDOG_ASPECT(symbol_join(t_ident, aspect), 1 * CLOCK_NS_PER_SEC, TELEMETRY_REPLICAS);
    static_repeat(TELEMETRY_REPLICAS, t_replica_id) {
        COMM_ENC_REGISTER(symbol_join(t_ident, encoder, t_replica_id), t_pipe, t_replica_id);
        tlm_registration_t * const symbol_join(t_ident, registrations, t_replica_id)[] = t_components;
        struct tlm_system_mut symbol_join(t_ident, mutable, t_replica_id) = {
            .async_dropped = 0,
        };
        tlm_replica_t symbol_join(t_ident, replica, t_replica_id) = {
            .mut = &symbol_join(t_ident, mutable, t_replica_id),
            .replica_id = t_replica_id,
            .comm_encoder = &symbol_join(t_ident, encoder, t_replica_id),
            .registrations = symbol_join(t_ident, registrations, t_replica_id),
            .num_registrations = PP_ARRAY_SIZE(symbol_join(t_ident, registrations, t_replica_id)),
            .aspect = &symbol_join(t_ident, aspect),
        };
        CLIP_REGISTER(symbol_join(t_ident, clip, t_replica_id),
                      telemetry_pump, &symbol_join(t_ident, replica, t_replica_id));
    }
}

macro_define(TELEMETRY_SCHEDULE, t_ident) {
    static_repeat(TELEMETRY_REPLICAS, t_replica_id) {
        CLIP_SCHEDULE(symbol_join(t_ident, clip, t_replica_id), 150)
    }
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
    };
    tlm_registration_t symbol_join(e_ident, reg) = {
        .replicas = {
            [0 ... TELEMETRY_REPLICAS-1] = {
                .is_synchronous = false,
                .async_duct = &symbol_join(e_ident, duct),
            },
        },
    }
}

macro_define(TELEMETRY_SYNC_REGISTER, e_ident, e_replicas, e_max_flow) {
    PIPE_REGISTER(symbol_join(e_ident, pipe), e_replicas, TELEMETRY_REPLICAS, e_max_flow, sizeof(tlm_sync_t),
                  PIPE_SENDER_FIRST);
    tlm_sync_t symbol_join(e_ident, sender_scratch)[e_replicas];
    tlm_endpoint_t e_ident = {
        .is_synchronous = true,
        .sync_pipe = &symbol_join(e_ident, pipe),
        .sender_scratch = symbol_join(e_ident, sender_scratch),
    };
    static_repeat(TELEMETRY_REPLICAS, replica_id) {
        CIRC_BUF_REGISTER(symbol_join(e_ident, receiver_scratch, replica_id), sizeof(tlm_sync_slot_t), e_max_flow);
    }
    tlm_registration_t symbol_join(e_ident, reg) = {
        .replicas = {
            static_repeat(TELEMETRY_REPLICAS, replica_id) {
                {
                    .is_synchronous = true,
                    .sync_pipe = &symbol_join(e_ident, pipe),
                    .receiver_scratch = &symbol_join(e_ident, receiver_scratch, replica_id),
                },
            }
        },
    }
}

macro_define(TELEMETRY_ENDPOINT_REF, e_ident) {
    &symbol_join(e_ident, reg),
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
