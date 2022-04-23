#ifndef FSW_FAKEWIRE_CODEC_H
#define FSW_FAKEWIRE_CODEC_H

#include <stdint.h>
#include <stdbool.h>

#include <synch/duct.h>

// THREAD SAFETY NOTE: none of this code is thread-safe.

typedef enum {
    FWC_NONE = 0,

    // these need to match the numbers used in Go, and not overlap when XORed with 0x10.
    FWC_HANDSHAKE_1  = 0x80, // parametrized(handshake_id)
    FWC_HANDSHAKE_2  = 0x81, // parametrized(handshake_id)
    FWC_START_PACKET = 0x82,
    FWC_END_PACKET   = 0x83,
    FWC_ERROR_PACKET = 0x84,
    FWC_FLOW_CONTROL = 0x85,
    FWC_KEEP_ALIVE   = 0x86,
    FWC_ESCAPE_SYM   = 0x87,

    // alias, because ESCAPE_SYM never needs to be passed to an upper layer
    FWC_CODEC_ERROR  = FWC_ESCAPE_SYM,
} fw_ctrl_t;

const char *fakewire_codec_symbol(fw_ctrl_t c);

static inline bool fakewire_is_special(uint8_t ch) {
    return ch >= FWC_HANDSHAKE_1 && ch <= FWC_ESCAPE_SYM;
}

static inline bool fakewire_is_parametrized(fw_ctrl_t ch) {
    assert(fakewire_is_special(ch));
    return ch == FWC_HANDSHAKE_1 || ch == FWC_HANDSHAKE_2 || ch == FWC_FLOW_CONTROL || ch == FWC_KEEP_ALIVE;
}

typedef struct {
    fw_ctrl_t     ctrl_out;
    uint32_t      ctrl_param;
    uint8_t      *data_out;     // pointer provided by caller; if NULL, data is discarded (but data_actual_len is still set)
    size_t        data_max_len; // max len provided by caller
    size_t        data_actual_len;
    local_time_t  receive_timestamp;
} fw_decoded_ent_t;

// this structure is incorporated into the exchange's synchronized notepad structure
typedef struct {
    // for internal decoder
    bool         recv_in_escape;
    // for external decoder
    fw_ctrl_t    recv_current; // parameterized control character
    size_t       recv_count;   // 0-3: N bytes already processed
    uint32_t     recv_param;
    local_time_t recv_timestamp_ns;
} fw_decoder_synch_t;

typedef const struct {
    struct fw_decoder_mut {
        size_t       rx_length;
        size_t       rx_offset;
        local_time_t rx_timestamp;
    } *mut;
    uint8_t  rx_duct_replica;
    duct_t  *rx_duct;
    uint8_t *rx_buffer;
    size_t   rx_buffer_capacity;
} fw_decoder_t;

// note: a decoder acts as the server side of data_rx
macro_define(FAKEWIRE_DECODER_REGISTER, d_ident, d_duct, d_replica, d_duct_size) {
    uint8_t symbol_join(d_ident, buffer)[d_duct_size];
    struct fw_decoder_mut symbol_join(d_ident, mut);
    fw_decoder_t d_ident = {
        .mut = &symbol_join(d_ident, mut),
        .rx_duct_replica = (d_replica),
        .rx_duct = &(d_duct),
        .rx_buffer = symbol_join(d_ident, buffer),
        .rx_buffer_capacity = (d_duct_size),
    }
}

void fakewire_dec_reset(fw_decoder_t *fwd, fw_decoder_synch_t *synch);

static inline size_t fakewire_dec_remaining_bytes(fw_decoder_t *fwd) {
    assert(fwd != NULL);
    assert(fwd->mut->rx_length >= fwd->mut->rx_offset);
    return fwd->mut->rx_length - fwd->mut->rx_offset;
}

void fakewire_dec_prepare(fw_decoder_t *fwd);
// returns true if another character is available; false if yielding is recommended
bool fakewire_dec_decode(fw_decoder_t *fwd, fw_decoder_synch_t *synch, fw_decoded_ent_t *decoded);
void fakewire_dec_commit(fw_decoder_t *fwd);

typedef const struct {
    struct fw_encoder_mut {
        size_t tx_offset;
    } *mut;
    uint8_t  tx_duct_replica;
    duct_t  *tx_duct;
    uint8_t *tx_buffer;
    size_t   tx_capacity;
} fw_encoder_t;

macro_define(FAKEWIRE_ENCODER_REGISTER, e_ident, e_duct, e_replica, e_duct_size) {
    uint8_t symbol_join(e_ident, buffer)[e_duct_size];
    struct fw_encoder_mut symbol_join(e_ident, mut);
    fw_encoder_t e_ident = {
        .mut = &symbol_join(e_ident, mut),
        .tx_duct_replica = (e_replica),
        .tx_duct = &(e_duct),
        .tx_buffer = symbol_join(e_ident, buffer),
        .tx_capacity = (e_duct_size),
    }
}

void fakewire_enc_prepare(fw_encoder_t *fwe);
// returns how many bytes were successfully written (possibly 0, in which case yielding is recommended)
size_t fakewire_enc_encode_data(fw_encoder_t *fwe, const uint8_t *bytes_in, size_t byte_count);
// returns true if control character was written, or false otherwise
bool fakewire_enc_encode_ctrl(fw_encoder_t *fwe, fw_ctrl_t symbol, uint32_t param);
void fakewire_enc_flush(fw_encoder_t *fwe);
void fakewire_enc_commit(fw_encoder_t *fwe);

#endif /* FSW_FAKEWIRE_CODEC_H */
