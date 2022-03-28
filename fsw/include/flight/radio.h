#ifndef FSW_RADIO_H
#define FSW_RADIO_H

#include <synch/pipe.h>
#include <bus/rmap.h>
#include <bus/switch.h>

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
    UPLINK_BUF_LOCAL_SIZE   = 0x1000,
    DOWNLINK_BUF_LOCAL_SIZE = 0x1000,

    REG_IO_BUFFER_SIZE = sizeof(uint32_t) * NUM_REGISTERS,
};

struct radio_uplink_reads {
    uint32_t prime_read_address;
    uint32_t prime_read_length;
    uint32_t flipped_read_address;
    uint32_t flipped_read_length;
    // may contain new values for REG_RX_PTR, REG_RX_LEN, REG_RX_PTR_ALT, REG_RX_LEN_ALT, REG_RX_STATE
    uint32_t new_registers[5];
    bool needs_update_all; // if set, then register array has new values for all five core registers written back
    bool needs_alt_update; // if set, then register array has new values for PTR_ALT and LEN_ALT only
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
    RAD_DL_VALIDATE_IDLE,
    RAD_DL_WRITE_RADIO_MEMORY,
    RAD_DL_START_TRANSMIT,
    RAD_DL_MONITOR_TRANSMIT,
    RAD_DL_VERIFY_COMPLETE,
};

typedef struct {
    // separate RMAP handlers so that the tasks can operate independently
    rmap_t        *rmap_up;
    rmap_t        *rmap_down;

    enum radio_uplink_state   uplink_state;
    struct radio_uplink_reads read_plan;
    enum radio_downlink_state downlink_state;
    uint32_t                  downlink_length;

    flag_t uplink_query_status_flag;

    uint32_t bytes_extracted;
    pipe_t  *up_pipe;
    pipe_t  *down_pipe;
    uint8_t  uplink_buf_local[UPLINK_BUF_LOCAL_SIZE];
    uint8_t  downlink_buf_local[DOWNLINK_BUF_LOCAL_SIZE];
} radio_t;

void radio_uplink_clip(radio_t *radio);
void radio_downlink_clip(radio_t *radio);

// uplink: ground -> spacecraft radio; downlink: spacecraft radio -> ground
#define RADIO_REGISTER(r_ident,     r_switch_in, r_switch_out,                                                        \
                       r_up_addr,   r_up_port,   r_up_capacity,   r_uplink,                                           \
                       r_down_addr, r_down_port, r_down_capacity, r_downlink)                                         \
    static_assert(REG_IO_BUFFER_SIZE <= (size_t) r_up_capacity                                                        \
                    && (size_t) r_up_capacity <= RMAP_MAX_DATA_LEN, "capacity check");                                \
    static_assert(REG_IO_BUFFER_SIZE <= (size_t) r_down_capacity                                                      \
                    && (size_t) r_down_capacity <= RMAP_MAX_DATA_LEN, "capacity check");                              \
    extern radio_t r_ident;                                                                                           \
    CLIP_REGISTER(r_ident ## _up_clip,   radio_uplink_clip,   &r_ident);                                              \
    CLIP_REGISTER(r_ident ## _down_clip, radio_downlink_clip, &r_ident);                                              \
    RMAP_ON_SWITCHES(r_ident ## _up,        "radio_up",   r_switch_in, r_switch_out, r_up_port,   r_up_addr,          \
                     UPLINK_BUF_LOCAL_SIZE, REG_IO_BUFFER_SIZE);                                                      \
    RMAP_ON_SWITCHES(r_ident ## _down,      "radio_down", r_switch_in, r_switch_out, r_down_port, r_down_addr,        \
                     REG_IO_BUFFER_SIZE,    DOWNLINK_BUF_LOCAL_SIZE);                                                 \
    radio_t r_ident = {                                                                                               \
        .rmap_up = &r_ident ## _up,                                                                                   \
        .rmap_down = &r_ident ## _down,                                                                               \
        .bytes_extracted = 0,                                                                                         \
        .uplink_state = RAD_UL_INITIAL_STATE,                                                                         \
        .read_plan = { },                                                                                             \
        .downlink_state = RAD_DL_INITIAL_STATE,                                                                       \
        .downlink_length = 0,                                                                                         \
        .up_pipe = &r_uplink,                                                                                         \
        .down_pipe = &r_downlink,                                                                                     \
        .uplink_buf_local = { 0 },                                                                                    \
        .downlink_buf_local = { 0 },                                                                                  \
    }

// two RMAP channels, so twice the flow
#define RADIO_MAX_IO_FLOW (2 * RMAP_MAX_IO_FLOW)

// largest packet size that the switch needs to be able to route
#define RADIO_MAX_IO_PACKET(r_up_capacity, r_down_capacity)                                                           \
    /* we know from the earlier assertions that these are the largest values for the capacities */                    \
    RMAP_MAX_IO_PACKET(r_up_capacity, r_down_capacity)

#define RADIO_UP_SCHEDULE(r_ident)                                                                                    \
    CLIP_SCHEDULE(r_ident ## _up_clip, 40)

#define RADIO_DOWN_SCHEDULE(r_ident)                                                                                  \
    CLIP_SCHEDULE(r_ident ## _down_clip, 50)

#endif /* FSW_RADIO_H */
