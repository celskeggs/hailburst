#ifndef FSW_FAKEWIRE_RMAP_H
#define FSW_FAKEWIRE_RMAP_H

#include <stdint.h>
#include <stdbool.h>

#include <hal/thread.h>
#include <bus/switch.h>

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
    RS_NO_RESPONSE         = 0x100,
    RS_READ_LENGTH_DIFFERS = 0x101,
    RS_INVALID_ERR         = 0xFFF, // used as a marker for error variables
} rmap_status_t;

typedef struct {
    const char * const label;
    duct_t     * const rx_duct;
    duct_t     * const tx_duct;
    uint8_t    * const scratch;

    const rmap_addr_t * const routing;

    uint16_t current_txn_id;
} rmap_t;

typedef struct {
    rmap_t    *rmap;
    duct_txn_t rx_recv_txn;
    duct_txn_t tx_send_txn;
} rmap_txn_t;

// maximum flow needed by an RMAP handler is one packet per epoch in each direction (for continuous operation)
#define RMAP_MAX_IO_FLOW        1

// a single-user RMAP handler; only one transaction may be in progress at a time.
// rx is for packets received by the RMAP handler; tx is for packets sent by the RMAP handler.
macro_define(RMAP_ON_SWITCHES,
             r_ident, r_label, r_switch_in, r_switch_out, r_switch_port, r_routing, r_max_read, r_max_write) {
    DUCT_REGISTER(symbol_join(r_ident, receive),      SWITCH_REPLICAS, 1, RMAP_MAX_IO_FLOW,
                  SCRATCH_MARGIN_READ  + r_max_read,  DUCT_SENDER_FIRST);
    DUCT_REGISTER(symbol_join(r_ident, transmit),     1, SWITCH_REPLICAS, RMAP_MAX_IO_FLOW,
                  SCRATCH_MARGIN_WRITE + r_max_write, DUCT_SENDER_FIRST);
    SWITCH_PORT_INBOUND(r_switch_out, r_switch_port, symbol_join(r_ident, transmit));
    SWITCH_PORT_OUTBOUND(r_switch_in, r_switch_port, symbol_join(r_ident, receive));
    uint8_t symbol_join(r_ident, scratch)[RMAP_MAX_IO_PACKET(r_max_read, r_max_write)];
    rmap_t r_ident = {
        .label = (r_label),
        .rx_duct = &symbol_join(r_ident, receive),
        .tx_duct = &symbol_join(r_ident, transmit),
        .scratch = symbol_join(r_ident, scratch),
        .routing = &(r_routing),
    }
}

macro_define(RMAP_MAX_IO_PACKET, r_max_read, r_max_write) {
    PP_CONST_MAX(SCRATCH_MARGIN_READ + (r_max_read), SCRATCH_MARGIN_WRITE + (r_max_write))
}

// must be called every epoch before any uses of RMAP have been made, even if RMAP won't be used.
void rmap_epoch_prepare(rmap_txn_t *txn, rmap_t *rmap);
// must be called every epoch after all uses of RMAP have been completed, even if RMAP didn't get used.
void rmap_epoch_commit(rmap_txn_t *txn);

// uses ACKNOWLEDGE | VERIFY | INCREMENT flags
void rmap_write_start(rmap_txn_t *txn, uint8_t ext_addr, uint32_t main_addr, uint8_t *buffer, size_t length);
// this should be called one epoch later, to give the networking infrastructure time to respond
rmap_status_t rmap_write_complete(rmap_txn_t *txn, local_time_t *ack_timestamp_out);

// uses INCREMENT flag
void rmap_read_start(rmap_txn_t *txn, uint8_t ext_addr, uint32_t main_addr, size_t length);
// this should be called one epoch later, to give the networking infrastructure time to respond
rmap_status_t rmap_read_complete(rmap_txn_t *txn, uint8_t *buffer, size_t buffer_size, local_time_t *ack_timestamp_out);

// helper functions for main code (defined in rmap_helpers.c)
uint8_t rmap_crc8(uint8_t *bytes, size_t len);
uint8_t rmap_crc8_extend(uint8_t previous, uint8_t *bytes, size_t len);
void rmap_encode_source_path(uint8_t **out, const rmap_path_t *path);

#endif /* FSW_FAKEWIRE_RMAP_H */
