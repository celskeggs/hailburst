#ifndef FSW_IO_H
#define FSW_IO_H

#include <stddef.h>
#include <stdint.h>

#include <fsw/chart.h>

struct io_rx_ent {
    uint64_t receive_timestamp;
    uint32_t actual_length;
    uint8_t  data[];
};

// returns the note size minus the header size... i.e., the actual maximum data length for a chart of io_rx_ent structs
static inline uint32_t io_rx_size(chart_t *chart) {
    return chart_note_size(chart) - offsetof(struct io_rx_ent, data);
}

struct io_tx_ent {
    uint32_t actual_length;
    uint8_t  data[];
};

// returns the note size minus the header size... i.e., the actual maximum data length for a chart of io_tx_ent structs
static inline uint32_t io_tx_size(chart_t *chart) {
    return chart_note_size(chart) - offsetof(struct io_tx_ent, data);
}

#endif /* FSW_IO_H */
