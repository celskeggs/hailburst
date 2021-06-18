#ifndef APP_FAKEWIRE_CODEC_H
#define APP_FAKEWIRE_CODEC_H

#include <stdint.h>
#include <stdbool.h>

#include "ringbuf.h"

// THREAD SAFETY NOTE: none of this code is thread-safe.
// You may free the memory used in any of these structures at any time, as long as the structure is not in use.

typedef enum {
    FWC_FCT  = 0x0,
    FWC_EOP  = 0x1,
    FWC_EEP  = 0x2,
    FWC_ESC  = 0x3,
} fw_ctrl_t;

typedef struct {
    void *param;
    void (*recv_data)(void *param, uint8_t *bytes_in, size_t bytes_count);
    void (*recv_ctrl)(void *param, fw_ctrl_t symbol);
    // if called, this is the last callback the receiver will receive
    void (*parity_fail)(void *param);
} fw_receiver_t;

typedef struct {
    fw_receiver_t *output;
    bool           parity_ok;
    uint8_t        stashed_bits;
    uint8_t        stashed;
} fw_decoder_t;

void fakewire_dec_init(fw_decoder_t *fwd, fw_receiver_t *output);
void fakewire_dec_decode(fw_decoder_t *fwd, uint8_t *bytes_in, size_t byte_count);
void fakewire_dec_eof(fw_decoder_t *fwd);

typedef struct {
    ringbuf_t *output;
    uint8_t    stashed_bits;
    uint8_t    stashed;
    uint8_t    last_remainder; // 1 if odd # of one bits, 0 if even # of one bits
} fw_encoder_t;

void fakewire_enc_init(fw_encoder_t *fwe, ringbuf_t *output);
void fakewire_enc_encode_data(fw_encoder_t *fwe, uint8_t *bytes_in, size_t byte_count);
void fakewire_enc_encode_ctrl(fw_encoder_t *fwe, fw_ctrl_t symbol);

#endif /* APP_FAKEWIRE_CODEC_H */