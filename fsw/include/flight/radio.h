#ifndef FSW_RADIO_H
#define FSW_RADIO_H

#include <hal/clip.h>
#include <hal/watchdog.h>
#include <synch/notepad.h>
#include <synch/pipe.h>
#include <bus/rmap.h>
#include <bus/switch.h>

// use default number of replicas
#define RADIO_REPLICAS CONFIG_APPLICATION_REPLICAS

typedef enum {
    REG_MAGIC      = 0,
    REG_MEM_BASE   = 1,
    REG_MEM_SIZE   = 2,
    REG_TX_PTR     = 3,
    REG_TX_LEN     = 4,
    REG_TX_STATE   = 5,
    REG_RX_PTR     = 6,
    REG_RX_LEN     = 7,
    REG_RX_PTR_ALT = 8,
    REG_RX_LEN_ALT = 9,
    REG_RX_STATE   = 10,
    REG_ERR_COUNT  = 11,
    NUM_REGISTERS  = 12,
} radio_register_t;

enum {
    RADIO_MAGIC         = 0x7E1ECA11,
    RADIO_REG_BASE_ADDR = 0x0000,
    RADIO_MEM_BASE_ADDR = 0x1000,
    RADIO_MEM_SIZE      = 0x2000,

    UPLINK_BUF_LOCAL_SIZE   = 0x500,
    DOWNLINK_BUF_LOCAL_SIZE = 0x500,

    REG_IO_BUFFER_SIZE = sizeof(uint32_t) * NUM_REGISTERS,
};

typedef struct {
    uint32_t base;
    uint32_t size;
} radio_memregion_t;

struct radio_uplink_reads {
    uint32_t prime_read_address;
    uint32_t prime_read_length;
    uint32_t flipped_read_address;
    uint32_t flipped_read_length;
    // may contain new values for REG_RX_PTR, REG_RX_LEN, REG_RX_PTR_ALT, REG_RX_LEN_ALT, REG_RX_STATE
    uint32_t new_registers[5];
    bool needs_update_all; // if set, then register array has new values for all five core registers written back
    bool needs_alt_update; // if set, then register array has new values for PTR_ALT and LEN_ALT only
    // side channel for specifying whether the radio watchdog aspect should be fed
    bool watchdog_ok;
};

enum radio_uplink_state {
    RAD_UL_INITIAL_STATE,
    RAD_UL_QUERY_COMMON_CONFIG,
    RAD_UL_DISABLE_RECEIVE,
    RAD_UL_RESET_REGISTERS,
    RAD_UL_QUERY_STATE,
    RAD_UL_PRIME_READ,
    RAD_UL_FLIPPED_READ,
    RAD_UL_REFILL_BUFFERS,
    RAD_UL_WRITE_TO_STREAM,
};

enum radio_downlink_state {
    RAD_DL_INITIAL_STATE,
    RAD_DL_QUERY_COMMON_CONFIG,
    RAD_DL_DISABLE_TRANSMIT,
    RAD_DL_WAITING_FOR_STREAM,
    RAD_DL_WRITE_RADIO_MEMORY,
    RAD_DL_START_TRANSMIT,
    RAD_DL_MONITOR_TRANSMIT,
};

struct radio_uplink_note {
    // automatically synchronized
    enum radio_uplink_state   uplink_state;
    struct radio_uplink_reads read_plan;
    uint32_t                  bytes_extracted;
    rmap_synch_t              rmap_synch;
};

typedef const struct {
    struct radio_uplink_mut {
        // not automatically synchronized
        flag_t  uplink_query_status_flag;
        uint8_t uplink_buf_local[UPLINK_BUF_LOCAL_SIZE];
    } *mut;
    uint8_t            replica_id;
    notepad_ref_t     *mut_synch;
    rmap_replica_t    *rmap_up;
    pipe_t            *up_pipe;
    watchdog_aspect_t *up_aspect;
} radio_uplink_replica_t;

struct radio_downlink_note {
    // automatically synchronized
    enum radio_downlink_state downlink_state;
    uint32_t                  downlink_length;
    rmap_synch_t              rmap_synch;
};

typedef const struct {
    struct radio_downlink_mut {
        // not automatically synchronized
        uint32_t downlink_length_local;
        uint8_t  downlink_buf_local[DOWNLINK_BUF_LOCAL_SIZE];
    } *mut;
    uint8_t            replica_id;
    notepad_ref_t     *mut_synch;
    rmap_replica_t    *rmap_down;
    pipe_t            *down_pipe;
    watchdog_aspect_t *down_aspect;
} radio_downlink_replica_t;

// internal common function
bool radio_validate_common_config(uint32_t config_data[3]);

void radio_uplink_clip(radio_uplink_replica_t *radio);
void radio_downlink_clip(radio_downlink_replica_t *radio);

// uplink: ground -> spacecraft radio; downlink: spacecraft radio -> ground

macro_define(RADIO_UPLINK_REGISTER, r_ident,   r_switch_in, r_switch_out,
                                    r_up_addr, r_up_port,   r_up_capacity, r_uplink) {
    static_assert(REG_IO_BUFFER_SIZE <= (size_t) r_up_capacity
                    && (size_t) r_up_capacity <= RMAP_MAX_DATA_LEN, "capacity check");
    RMAP_ON_SWITCHES(symbol_join(r_ident, rmap_up), RADIO_REPLICAS, r_switch_in, r_switch_out,
                     r_up_port, r_up_addr, UPLINK_BUF_LOCAL_SIZE, REG_IO_BUFFER_SIZE);
    WATCHDOG_ASPECT(symbol_join(r_ident, up_aspect), 1 * CLOCK_NS_PER_SEC, RADIO_REPLICAS);
    NOTEPAD_REGISTER(symbol_join(r_ident, up_notepad), RADIO_REPLICAS, sizeof(struct radio_uplink_note));
    static_repeat(RADIO_REPLICAS, r_replica_id) {
        struct radio_uplink_mut symbol_join(r_ident, uplink, r_replica_id, mut) = {
            .uplink_query_status_flag = FLAG_INITIALIZER,
            .uplink_buf_local = { 0 },
        };
        radio_uplink_replica_t symbol_join(r_ident, uplink, r_replica_id) = {
            .mut = &symbol_join(r_ident, uplink, r_replica_id, mut),
            .replica_id = r_replica_id,
            .mut_synch = NOTEPAD_REPLICA_REF(symbol_join(r_ident, up_notepad), r_replica_id),
            .rmap_up = RMAP_REPLICA_REF(symbol_join(r_ident, rmap_up), r_replica_id),
            .up_pipe = &r_uplink,
            .up_aspect = &symbol_join(r_ident, up_aspect),
        };
        CLIP_REGISTER(symbol_join(r_ident, up_clip, r_replica_id),
                      radio_uplink_clip, &symbol_join(r_ident, uplink, r_replica_id));
    }
}

macro_define(RADIO_DOWNLINK_REGISTER, r_ident,     r_switch_in, r_switch_out,
                                      r_down_addr, r_down_port, r_down_capacity, r_downlink) {
    static_assert(REG_IO_BUFFER_SIZE <= (size_t) r_down_capacity
                    && (size_t) r_down_capacity <= RMAP_MAX_DATA_LEN, "capacity check");
    RMAP_ON_SWITCHES(symbol_join(r_ident, rmap_down), RADIO_REPLICAS, r_switch_in, r_switch_out,
                     r_down_port, r_down_addr, REG_IO_BUFFER_SIZE, DOWNLINK_BUF_LOCAL_SIZE);
    WATCHDOG_ASPECT(symbol_join(r_ident, down_aspect), 1 * CLOCK_NS_PER_SEC, RADIO_REPLICAS);
    NOTEPAD_REGISTER(symbol_join(r_ident, down_notepad), RADIO_REPLICAS, sizeof(struct radio_downlink_note));
    static_repeat(RADIO_REPLICAS, r_replica_id) {
        struct radio_downlink_mut symbol_join(r_ident, downlink, r_replica_id, mut) = {
            .downlink_length_local = 0,
            .downlink_buf_local = { 0 },
        };
        radio_downlink_replica_t symbol_join(r_ident, downlink, r_replica_id) = {
            .mut = &symbol_join(r_ident, downlink, r_replica_id, mut),
            .replica_id = r_replica_id,
            .mut_synch = NOTEPAD_REPLICA_REF(symbol_join(r_ident, down_notepad), r_replica_id),
            .rmap_down = RMAP_REPLICA_REF(symbol_join(r_ident, rmap_down), r_replica_id),
            .down_pipe = &r_downlink,
            .down_aspect = &symbol_join(r_ident, down_aspect),
        };
        CLIP_REGISTER(symbol_join(r_ident, down_clip, r_replica_id),
                      radio_downlink_clip, &symbol_join(r_ident, downlink, r_replica_id));
    }
}

macro_define(RADIO_REGISTER, r_ident,     r_switch_in, r_switch_out,
                             r_up_addr,   r_up_port,   r_up_capacity,   r_uplink,
                             r_down_addr, r_down_port, r_down_capacity, r_downlink) {
    RADIO_UPLINK_REGISTER(r_ident,   r_switch_in, r_switch_out, r_up_addr,   r_up_port,   r_up_capacity,   r_uplink);
    RADIO_DOWNLINK_REGISTER(r_ident, r_switch_in, r_switch_out, r_down_addr, r_down_port, r_down_capacity, r_downlink)
}

// two RMAP channels, so twice the flow
#define RADIO_MAX_IO_FLOW (2 * RMAP_MAX_IO_FLOW)

// largest packet size that the switch needs to be able to route
#define RADIO_MAX_IO_PACKET(r_up_capacity, r_down_capacity)                                                           \
    /* we know from the earlier assertions that these are the largest values for the capacities */                    \
    RMAP_MAX_IO_PACKET(r_up_capacity, r_down_capacity)

macro_define(RADIO_UP_SCHEDULE, r_ident) {
    static_repeat(RADIO_REPLICAS, r_replica_id) {
        CLIP_SCHEDULE(symbol_join(r_ident, up_clip, r_replica_id), 40)
    }
}

macro_define(RADIO_DOWN_SCHEDULE, r_ident) {
    static_repeat(RADIO_REPLICAS, r_replica_id) {
        CLIP_SCHEDULE(symbol_join(r_ident, down_clip, r_replica_id), 70)
    }
}

macro_define(RADIO_WATCH, r_ident) {
    &symbol_join(r_ident, up_aspect),
    &symbol_join(r_ident, down_aspect),
}

#endif /* FSW_RADIO_H */
