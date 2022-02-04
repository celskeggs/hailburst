#ifndef FSW_IO_H
#define FSW_IO_H

#include <stddef.h>
#include <stdint.h>

#include <synch/chart.h>
#include <synch/vochart.h>

struct io_rx_ent {
    uint64_t receive_timestamp;
    uint32_t actual_length;
    uint8_t  data[];
};

#define IO_RX_ASSERT_SIZE(note_size, rx_size)                                                                         \
    static_assert(note_size >= offsetof(struct io_rx_ent, data) + rx_size)

// adds the header size to the desired data buffer size
#define io_rx_pad_size(size) ((size) + offsetof(struct io_rx_ent, data))

// returns the note size minus the header size... i.e., the actual maximum data length for a chart of io_rx_ent structs
static inline uint32_t io_rx_size(chart_t *chart) {
    uint32_t size = chart_note_size(chart);
    assert(size >= offsetof(struct io_rx_ent, data));
    return size - offsetof(struct io_rx_ent, data);
}

static inline uint32_t io_rx_size_vc(vochart_client_t *chart) {
    uint32_t size = vochart_client_note_size(chart);
    assert(size >= offsetof(struct io_rx_ent, data));
    return size - offsetof(struct io_rx_ent, data);
}

static inline uint32_t io_rx_size_vs(vochart_server_t *chart) {
    uint32_t size = vochart_server_note_size(chart);
    assert(size >= offsetof(struct io_rx_ent, data));
    return size - offsetof(struct io_rx_ent, data);
}

struct io_tx_ent {
    uint32_t actual_length;
    uint8_t  data[];
};

// adds the header size to the desired data buffer size
#define io_tx_pad_size(size) ((size) + offsetof(struct io_tx_ent, data))

// returns the note size minus the header size... i.e., the actual maximum data length for a chart of io_tx_ent structs
static inline uint32_t io_tx_size(chart_t *chart) {
    uint32_t size = chart_note_size(chart);
    assert(size >= offsetof(struct io_tx_ent, data));
    return size - offsetof(struct io_tx_ent, data);
}

#endif /* FSW_IO_H */
