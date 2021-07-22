#include <assert.h>
#include <string.h>

#include "debug.h"
#include "fakewire_codec.h"

void fakewire_dec_init(fw_decoder_t *fwd, fw_receiver_t *output) {
    assert(fwd != NULL && output != NULL);
    fwd->output = output;
    fwd->in_escape = false;
}

static inline bool fakewire_is_special(uint8_t ch) {
    return ch >= FWC_HANDSHAKE_1 && ch <= FWC_ESCAPE_SYM;
}

void fakewire_dec_decode(fw_decoder_t *fwd, uint8_t *bytes_in, size_t byte_count) {
    assert(byte_count > 0);

    uint8_t databuf[256];
    size_t db_index = 0;

    for (size_t byte_i = 0; byte_i < byte_count; byte_i++) {
        uint8_t cur_byte = bytes_in[byte_i];

        fw_ctrl_t ctrl_char = FWC_NONE;
        bool consumed = false;

        if (fwd->in_escape) {
            fwd->in_escape = false;
            uint8_t decoded = cur_byte ^ 0x10;
            if (fakewire_is_special(decoded)) {
                // valid escape sequence
                cur_byte = decoded;
            } else {
                // invalid escape sequence; pass the escape up the line for error handling
                ctrl_char = FWC_ESCAPE_SYM;
            }
        }
        if (fakewire_is_special(cur_byte)) {
            if (cur_byte == FWC_ESCAPE_SYM) {
                // handle escape sequence for next byte
                fwd->in_escape = true;
            } else {
                // pass control character up the line
                ctrl_char = cur_byte;
            }
            consumed = true;
        }

        // transmit data as necessary
        if ((ctrl_char != FWC_NONE && db_index > 0) || db_index >= sizeof(databuf)) {
            fwd->output->recv_data(fwd->output->param, databuf, db_index);
            db_index = 0;
        }
        // transmit control characters
        if (ctrl_char != FWC_NONE) {
            fwd->output->recv_ctrl(fwd->output->param, ctrl_char);
        }
        // append new data to buffer
        if (!consumed) {
            databuf[db_index++] = cur_byte;
        }
    }

    // if any data remains, push it to the receiver.
    if (db_index > 0) {
        fwd->output->recv_data(fwd->output->param, databuf, db_index);
    }
}

void fakewire_enc_init(fw_encoder_t *fwe, ringbuf_t *output) {
    assert(fwe != NULL && output != NULL);
    assert(ringbuf_elem_size(output) == 1);
    fwe->output = output;
}

void fakewire_enc_encode_data(fw_encoder_t *fwe, uint8_t *bytes_in, size_t byte_count) {
    assert(fwe != NULL && bytes_in != NULL);
    assert(byte_count > 0);
    // allocate enough space for byte_count * 2 bytes
    uint8_t temp[byte_count * 2];
    size_t j = 0;

    for (size_t i = 0; i < byte_count; i++) {
        uint8_t byte = bytes_in[i];
        if (fakewire_is_special(byte)) {
            temp[j++] = FWC_ESCAPE_SYM;
            // encode byte so that it remains in the data range
            byte ^= 0x10;
        }
        temp[j++] = byte;
    }
    assert(j >= byte_count && j <= sizeof(temp));

    // write data to ring buffer
    ringbuf_write_all(fwe->output, temp, j);
}

void fakewire_enc_encode_ctrl(fw_encoder_t *fwe, fw_ctrl_t symbol) {
    assert(fwe != NULL);
    assert(fakewire_is_special(symbol) && symbol != FWC_ESCAPE_SYM);

    uint8_t byte = symbol;
    ringbuf_write_all(fwe->output, &byte, 1);
}
