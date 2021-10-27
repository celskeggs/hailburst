#ifndef FSW_FAKEWIRE_CODEC_H
#define FSW_FAKEWIRE_CODEC_H

#include <stdint.h>
#include <stdbool.h>

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
    void *param;
    void (*recv_data)(void *opaque, uint8_t *bytes_in, size_t bytes_count);
    void (*recv_ctrl)(void *opaque, fw_ctrl_t symbol, uint32_t param, uint64_t recv_timestamp_ns);
} fw_receiver_t;

typedef struct {
    fw_receiver_t *output;
    bool in_escape;

    fw_ctrl_t recv_current; // parameterized control character
    size_t    recv_count;   // 0-3: N bytes already processed
    uint32_t  recv_param;
    uint64_t  recv_timestamp_ns;
} fw_decoder_t;

void fakewire_dec_init(fw_decoder_t *fwd, fw_receiver_t *output);
// no destroy function provided because it isn't needed; you can simply stop using the decoder.
void fakewire_dec_decode(fw_decoder_t *fwd, uint8_t *bytes_in, size_t byte_count, uint64_t recv_timestamp_ns);

typedef void (*fw_output_cb_t)(void *param, uint8_t *bytes_in, size_t byte_count);

typedef struct {
    void          *output_param;
    fw_output_cb_t output_cb;

    uint8_t       *enc_buffer;
    size_t         enc_idx;
} fw_encoder_t;

void fakewire_enc_init(fw_encoder_t *fwe, fw_output_cb_t output_cb, void *output_param);
// TODO: add destroy function, because it is now necessary

void fakewire_enc_encode_data(fw_encoder_t *fwe, uint8_t *bytes_in, size_t byte_count);
void fakewire_enc_encode_ctrl(fw_encoder_t *fwe, fw_ctrl_t symbol, uint32_t param);
void fakewire_enc_flush(fw_encoder_t *fwe);

#endif /* FSW_FAKEWIRE_CODEC_H */
