#ifndef FSW_RADIO_H
#define FSW_RADIO_H

#include <synch/stream.h>
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

typedef struct {
    // separate RMAP handlers so that the tasks can operate independently
    rmap_t        *rmap_up;
    rmap_t        *rmap_down;

    uint32_t       bytes_extracted;
    stream_t      *up_stream;
    stream_t      *down_stream;
    uint8_t        uplink_buf_local[UPLINK_BUF_LOCAL_SIZE];
    uint8_t        downlink_buf_local[DOWNLINK_BUF_LOCAL_SIZE];
} radio_t;

void radio_uplink_loop(radio_t *radio);
void radio_downlink_loop(radio_t *radio);

// uplink: ground -> spacecraft radio; downlink: spacecraft radio -> ground
#define RADIO_REGISTER(r_ident,     r_switch_in, r_switch_out,                                                        \
                       r_up_addr,   r_up_port,   r_up_capacity,   r_uplink,                                           \
                       r_down_addr, r_down_port, r_down_capacity, r_downlink)                                         \
    static_assert(REG_IO_BUFFER_SIZE <= (size_t) r_up_capacity                                                        \
                    && (size_t) r_up_capacity <= RMAP_MAX_DATA_LEN, "capacity check");                                \
    static_assert(REG_IO_BUFFER_SIZE <= (size_t) r_down_capacity                                                      \
                    && (size_t) r_down_capacity <= RMAP_MAX_DATA_LEN, "capacity check");                              \
    extern radio_t r_ident;                                                                                           \
    TASK_REGISTER(r_ident ## _up_task,   radio_uplink_loop,   &r_ident, RESTARTABLE);                                 \
    TASK_REGISTER(r_ident ## _down_task, radio_downlink_loop, &r_ident, RESTARTABLE);                                 \
    RMAP_ON_SWITCHES(r_ident ## _up,        "radio_up",   r_switch_in, r_switch_out, r_up_port,   r_up_addr,          \
                     UPLINK_BUF_LOCAL_SIZE, REG_IO_BUFFER_SIZE);                                                      \
    RMAP_ON_SWITCHES(r_ident ## _down,      "radio_down", r_switch_in, r_switch_out, r_down_port, r_down_addr,        \
                     REG_IO_BUFFER_SIZE,    DOWNLINK_BUF_LOCAL_SIZE);                                                 \
    radio_t r_ident = {                                                                                               \
        .rmap_up = &r_ident ## _up,                                                                                   \
        .rmap_down = &r_ident ## _down,                                                                               \
        .bytes_extracted = 0,                                                                                         \
        .up_stream = &r_uplink,                                                                                       \
        .down_stream = &r_downlink,                                                                                   \
        .uplink_buf_local = { 0 },                                                                                    \
        .downlink_buf_local = { 0 },                                                                                  \
    };                                                                                                                \
    static void r_ident ## _init(void) {                                                                              \
        stream_set_writer(&r_uplink, &r_ident ## _up_task);                                                           \
        stream_set_reader(&r_downlink, &r_ident ## _down_task);                                                       \
    }                                                                                                                 \
    PROGRAM_INIT(STAGE_CRAFT, r_ident ## _init)

// two RMAP channels, so twice the flow
#define RADIO_MAX_IO_FLOW (2 * RMAP_MAX_IO_FLOW)

// largest packet size that the switch needs to be able to route
#define RADIO_MAX_IO_PACKET(r_up_capacity, r_down_capacity)                                                           \
    /* we know from the earlier assertions that these are the largest values for the capacities */                    \
    RMAP_MAX_IO_PACKET(r_up_capacity, r_down_capacity)

#define RADIO_UP_SCHEDULE(r_ident)                                                                                    \
    TASK_SCHEDULE(r_ident ## _up_task, 150)

#define RADIO_DOWN_SCHEDULE(r_ident)                                                                                  \
    TASK_SCHEDULE(r_ident ## _down_task, 150)

#endif /* FSW_RADIO_H */
