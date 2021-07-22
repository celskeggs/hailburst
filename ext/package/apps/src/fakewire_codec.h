#ifndef APP_FAKEWIRE_CODEC_H
#define APP_FAKEWIRE_CODEC_H

#include <stdint.h>
#include <stdbool.h>

#include "ringbuf.h"

// THREAD SAFETY NOTE: none of this code is thread-safe.
// You may free the memory used in any of these structures at any time, as long as the structure is not in use.

typedef enum {
    FWC_NONE = 0,

    FWC_HANDSHAKE_1  = 0x80,
    FWC_HANDSHAKE_2  = 0x81,
    FWC_START_PACKET = 0x82,
    FWC_END_PACKET   = 0x83,
    FWC_ERROR_PACKET = 0x84,
    FWC_FLOW_CONTROL = 0x85,
    FWC_ESCAPE_SYM   = 0x86,
} fw_ctrl_t;

typedef struct {
    void *param;
    void (*recv_data)(void *param, uint8_t *bytes_in, size_t bytes_count);
    void (*recv_ctrl)(void *param, fw_ctrl_t symbol);
} fw_receiver_t;

typedef struct {
    fw_receiver_t *output;
    bool in_escape;
} fw_decoder_t;

void fakewire_dec_init(fw_decoder_t *fwd, fw_receiver_t *output);
// no destroy function provided because it isn't needed; you can simply stop using the decoder.
void fakewire_dec_decode(fw_decoder_t *fwd, uint8_t *bytes_in, size_t byte_count);

typedef struct {
    ringbuf_t *output;
} fw_encoder_t;

void fakewire_enc_init(fw_encoder_t *fwe, ringbuf_t *output);
// no destroy function provided because it isn't needed; you can simply stop using the encoder.
void fakewire_enc_encode_data(fw_encoder_t *fwe, uint8_t *bytes_in, size_t byte_count);
void fakewire_enc_encode_ctrl(fw_encoder_t *fwe, fw_ctrl_t symbol);

#endif /* APP_FAKEWIRE_CODEC_H */