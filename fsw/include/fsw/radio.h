#ifndef FSW_RADIO_H
#define FSW_RADIO_H

#include <fsw/fakewire/rmap.h>
#include <fsw/ringbuf.h>

typedef struct {
    uint32_t base;
    uint32_t size;
} memregion_t;

typedef struct {
    rmap_context_t up_ctx;
    rmap_context_t down_ctx;
    rmap_addr_t    address;
    uint32_t       mem_access_base;
    memregion_t    rx_halves[2];
    memregion_t    tx_region;
    uint32_t       bytes_extracted;
    ringbuf_t     *up_ring;
    ringbuf_t     *down_ring;
    thread_t       up_thread;
    thread_t       down_thread;
    uint8_t       *uplink_buf_local;
    uint8_t       *downlink_buf_local;
} radio_t;

// uplink: ground -> spacecraft radio; downlink: spacecraft radio -> ground
void radio_init(radio_t *radio, rmap_monitor_t *mon, rmap_addr_t *address, ringbuf_t *uplink, ringbuf_t *downlink);

#endif /* FSW_RADIO_H */
