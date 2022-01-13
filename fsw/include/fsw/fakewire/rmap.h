#ifndef FSW_FAKEWIRE_RMAP_H
#define FSW_FAKEWIRE_RMAP_H

#include <stdint.h>
#include <stdbool.h>

#include <hal/thread.h>
#include <fsw/fakewire/switch.h>

enum {
    RMAP_MAX_PATH = 12,
    RMAP_MAX_DATA_LEN = 0x00FFFFFF,

    SCRATCH_MARGIN_WRITE = RMAP_MAX_PATH + 4 + RMAP_MAX_PATH + 12 + 1, // for write requests (larger than read)
    SCRATCH_MARGIN_READ = 12 + 1,                                      // for read replies (larger than write)
};

typedef struct {
    uint8_t *path_bytes;
    uint8_t num_path_bytes;
    uint8_t logical_address;
} rmap_path_t;

typedef struct {
    rmap_path_t destination;
    rmap_path_t source;
    uint8_t dest_key;
} rmap_addr_t;

typedef enum {
    RF_RESERVED    = 0x80,
    RF_COMMAND     = 0x40,
    RF_WRITE       = 0x20,
    RF_VERIFY      = 0x10,
    RF_ACKNOWLEDGE = 0x08,
    RF_INCREMENT   = 0x04,
    RF_SOURCEPATH  = 0x03,
} rmap_flags_t;

typedef enum {
    RS_OK                  = 0x000,
    RS_REMOTE_ERR_MIN      = 0x001,
    RS_REMOTE_ERR_MAX      = 0x0FF,
    RS_TRANSACTION_TIMEOUT = 0x100,
    RS_TRANSMIT_TIMEOUT    = 0x101,
    RS_TRANSMIT_BLOCKED    = 0x102,
    RS_READ_LENGTH_DIFFERS = 0x103,
    RS_INVALID_ERR         = 0xFFF, // used as a marker for error variables
} rmap_status_t;

typedef struct {
    semaphore_t *wake_rmap;
    chart_t     *rx_chart;
    chart_t     *tx_chart;

    uint8_t *body_pointer;
    bool     lingering_read;

    uint8_t            current_txn_flags;
    uint16_t           current_txn_id;
    const rmap_addr_t *current_routing;
} rmap_t;

void rmap_notify_wake(rmap_t *rmap);

// a single-user RMAP handler; only one transaction may be in progress at a time.
// rx is for packets received by the RMAP handler; tx is for packets sent by the RMAP handler.
#define RMAP_REGISTER(r_ident, r_max_read, r_max_write, r_receive, r_transmit)         \
    SEMAPHORE_REGISTER(r_ident ## _semaphore);                                         \
    CHART_REGISTER(r_receive, io_rx_pad_size(SCRATCH_MARGIN_READ + r_max_read), 2);    \
    CHART_REGISTER(r_transmit, io_rx_pad_size(SCRATCH_MARGIN_WRITE + r_max_write), 2); \
    rmap_t r_ident = {                                                                 \
        .wake_rmap = &r_ident ## _semaphore,                                           \
        .rx_chart = &r_receive,                                                        \
        .tx_chart = &r_transmit,                                                       \
    };                                                                                 \
    CHART_SERVER_NOTIFY(r_receive, rmap_notify_wake, &r_ident);                        \
    CHART_CLIENT_NOTIFY(r_transmit, rmap_notify_wake, &r_ident);

// returns a pointer into which up to max_write_length bytes can be written.
rmap_status_t rmap_write_prepare(rmap_t *rmap, const rmap_addr_t *routing, rmap_flags_t flags,
                                 uint8_t ext_addr, uint32_t main_addr, uint8_t **ptr_out);
// performs a prepared write, with the specified data length.
rmap_status_t rmap_write_commit(rmap_t *rmap, size_t data_length, uint64_t *ack_timestamp_out);
// performs a complete write
rmap_status_t rmap_write_exact(rmap_t *rmap, const rmap_addr_t *routing, rmap_flags_t flags,
                               uint8_t ext_addr, uint32_t main_addr, size_t length, uint8_t *input,
                               uint64_t *ack_timestamp_out);
// performs a read of up to a certain size, and outputs the actual size and a pointer to the received data.
// the pointer will be valid until the next call to any rmap function on this context.
rmap_status_t rmap_read_fetch(rmap_t *rmap, const rmap_addr_t *routing, rmap_flags_t flags,
                              uint8_t ext_addr, uint32_t main_addr, size_t *length, uint8_t **ptr_out);
// performs a read of up to a certain size, and copies the result into the specified buffer
rmap_status_t rmap_read_exact(rmap_t *rmap, const rmap_addr_t *routing, rmap_flags_t flags,
                              uint8_t ext_addr, uint32_t main_addr, size_t length, uint8_t *output);

// helper functions for main code (defined in rmap_helpers.c)
uint8_t rmap_crc8(uint8_t *bytes, size_t len);
uint8_t rmap_crc8_extend(uint8_t previous, uint8_t *bytes, size_t len);
void rmap_encode_source_path(uint8_t **out, const rmap_path_t *path);

#endif /* FSW_FAKEWIRE_RMAP_H */
