#ifndef FSW_RADIO_H
#define FSW_RADIO_H

#include <fsw/stream.h>
#include <fsw/fakewire/rmap.h>

typedef enum {
    REG_MAGIC      = 0,
    REG_TX_PTR     = 1,
    REG_TX_LEN     = 2,
    REG_TX_STATE   = 3,
    REG_RX_PTR     = 4,
    REG_RX_LEN     = 5,
    REG_RX_PTR_ALT = 6,
    REG_RX_LEN_ALT = 7,
    REG_RX_STATE   = 8,
    REG_ERR_COUNT  = 9,
    REG_MEM_BASE   = 10,
    REG_MEM_SIZE   = 11,
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
    // addresses differ by source address
    rmap_addr_t    address_up;
    rmap_addr_t    address_down;

    uint32_t       bytes_extracted;
    stream_t      *up_stream;
    stream_t      *down_stream;
    uint8_t        uplink_buf_local[UPLINK_BUF_LOCAL_SIZE];
    uint8_t        downlink_buf_local[DOWNLINK_BUF_LOCAL_SIZE];
} radio_t;

void radio_uplink_loop(radio_t *radio);
void radio_downlink_loop(radio_t *radio);

// uplink: ground -> spacecraft radio; downlink: spacecraft radio -> ground
#define RADIO_REGISTER(r_ident, r_up_addr, r_up_rx, r_up_tx, r_up_capacity,                                       \
                                r_down_addr, r_down_rx, r_down_tx, r_down_capacity, r_uplink, r_downlink)         \
    static_assert(REG_IO_BUFFER_SIZE <= (size_t) r_up_capacity                                                    \
                    && (size_t) r_up_capacity <= RMAP_MAX_DATA_LEN, "capacity check");                            \
    static_assert(REG_IO_BUFFER_SIZE <= (size_t) r_down_capacity                                                  \
                    && (size_t) r_down_capacity <= RMAP_MAX_DATA_LEN, "capacity check");                          \
    extern radio_t r_ident;                                                                                       \
    TASK_REGISTER(r_ident ## _up_task, "radio_up_loop", radio_uplink_loop, &r_ident, RESTARTABLE);                \
    TASK_REGISTER(r_ident ## _down_task, "radio_down_loop", radio_downlink_loop, &r_ident, RESTARTABLE);          \
    RMAP_REGISTER(r_ident ## _up,   r_up_capacity,   REG_IO_BUFFER_SIZE,                                          \
                                    r_up_rx,   r_up_tx,   r_ident ## _up_task);                                   \
    RMAP_REGISTER(r_ident ## _down, REG_IO_BUFFER_SIZE, r_down_capacity,                                          \
                                    r_down_rx, r_down_tx, r_ident ## _down_task);                                 \
    radio_t r_ident = {                                                                                           \
        .rmap_up = &r_ident ## _up,                                                                               \
        .rmap_down = &r_ident ## _down,                                                                           \
        .address_up = (r_up_addr),                                                                                \
        .address_down = (r_down_addr),                                                                            \
        .bytes_extracted = 0,                                                                                     \
        .up_stream = &r_uplink,                                                                                   \
        .down_stream = &r_downlink,                                                                               \
        .uplink_buf_local = { 0 },                                                                                \
        .downlink_buf_local = { 0 },                                                                              \
    };                                                                                                            \
    static void r_ident ## _init(void) {                                                                          \
        stream_set_writer(&r_uplink, &r_ident ## _up_task);                                                       \
        stream_set_reader(&r_downlink, &r_ident ## _down_task);                                                   \
    }                                                                                                             \
    PROGRAM_INIT(STAGE_CRAFT, r_ident ## _init)

#define RADIO_UP_SCHEDULE(r_ident)                                                                                \
    TASK_SCHEDULE(r_ident ## _up_task)

#define RADIO_DOWN_SCHEDULE(r_ident)                                                                              \
    TASK_SCHEDULE(r_ident ## _down_task)

#endif /* FSW_RADIO_H */
