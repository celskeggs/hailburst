#ifndef FSW_RADIO_H
#define FSW_RADIO_H

#include <fsw/fakewire/rmap.h>

typedef struct {
    uint32_t base;
    uint32_t size;
} memregion_t;

typedef struct {
    // separate RMAP handlers so that the tasks can operate independently
    rmap_t         rmap_up;
    rmap_t         rmap_down;
    // addresses differ by source address
    rmap_addr_t    address_up;
    rmap_addr_t    address_down;

    // TODO: eliminate these regions, because they are now computable at compile time
    memregion_t    rx_halves[2];
    memregion_t    tx_region;
    uint32_t       bytes_extracted;
    stream_t      *up_stream;
    stream_t      *down_stream;
    thread_t       up_thread;
    thread_t       down_thread;
    uint8_t       *uplink_buf_local;
    uint8_t       *downlink_buf_local;
} radio_t;

// uplink: ground -> spacecraft radio; downlink: spacecraft radio -> ground
void radio_init(radio_t *radio,
                rmap_addr_t *up_addr, chart_t **up_rx_out, chart_t **up_tx_out, size_t uplink_capacity,
                rmap_addr_t *down_addr, chart_t **down_rx_out, chart_t **down_tx_out, size_t downlink_capacity,
                stream_t *uplink, stream_t *downlink);

void radio_start(radio_t *radio);

#endif /* FSW_RADIO_H */
