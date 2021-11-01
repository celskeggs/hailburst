#ifndef FSW_FAKEWIRE_CODEC_H
#define FSW_FAKEWIRE_CODEC_H

#include <stdint.h>
#include <stdbool.h>

#include <fsw/chart.h>
#include <fsw/io.h>

// THREAD SAFETY NOTE: none of this code is thread-safe.
// You may free the memory used in any of these structures at any time, as long as the structure is not in use.

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
    fw_ctrl_t ctrl_out;
    uint32_t  ctrl_param;
    uint8_t  *data_out;     // pointer provided by caller; if NULL, data is discarded (but data_actual_len is still set)
    size_t    data_max_len; // max len provided by caller
    size_t    data_actual_len;
    uint64_t  receive_timestamp;
} fw_decoded_ent_t;

typedef struct {
    chart_t          *rx_chart;
    struct io_rx_ent *rx_entry;
    uint32_t          rx_offset;

    // for internal decoder
    bool recv_in_escape;
    // for external decoder
    fw_ctrl_t recv_current; // parameterized control character
    size_t    recv_count;   // 0-3: N bytes already processed
    uint32_t  recv_param;
    uint64_t  recv_timestamp_ns;
} fw_decoder_t;

// note: a decoder acts as the server side of data_rx
void fakewire_dec_init(fw_decoder_t *fwd, chart_t *rx_chart);
// no destroy function provided because it isn't needed; you can simply stop using the decoder.

// returns true if another character is available; false if waiting on the chart is recommended
bool fakewire_dec_decode(fw_decoder_t *fwd, fw_decoded_ent_t *decoded);

typedef struct {
    chart_t          *tx_chart;
    struct io_tx_ent *tx_entry;

    // set if we've written something besides just data bytes and START_PACKET characters
    // (the idea is that there's no point in flushing those unless we also have other characters worth flushing)
    bool is_flush_worthwhile;
} fw_encoder_t;

void fakewire_enc_init(fw_encoder_t *fwe, chart_t *tx_chart);
// no destroy function provided because it isn't needed; you can simply stop using the encoder.

// returns how many bytes were successfully written (possibly 0, in which case waiting on the chart is recommended)
size_t fakewire_enc_encode_data(fw_encoder_t *fwe, const uint8_t *bytes_in, size_t byte_count);
// returns true if control character was written, or false otherwise
bool fakewire_enc_encode_ctrl(fw_encoder_t *fwe, fw_ctrl_t symbol, uint32_t param);
void fakewire_enc_flush(fw_encoder_t *fwe);

#endif /* FSW_FAKEWIRE_CODEC_H */
