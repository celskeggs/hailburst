#include <assert.h>
#include <endian.h>
#include <string.h>

#include <fsw/debug.h>
#include <fsw/fakewire/codec.h>

//#define DEBUG

void fakewire_dec_init(fw_decoder_t *fwd, fw_receiver_t *output) {
    assert(fwd != NULL && output != NULL);
    fwd->output = output;
    fwd->in_escape = false;
}

const char *fakewire_codec_symbol(fw_ctrl_t c) {
    switch (c) {
    case FWC_HANDSHAKE_1:
        return "HANDSHAKE_1";
    case FWC_HANDSHAKE_2:
        return "HANDSHAKE_2";
    case FWC_START_PACKET:
        return "START_PACKET";
    case FWC_END_PACKET:
        return "END_PACKET";
    case FWC_ERROR_PACKET:
        return "ERROR_PACKET";
    case FWC_FLOW_CONTROL:
        return "FLOW_CONTROL";
    case FWC_KEEP_ALIVE:
        return "KEEP_ALIVE";
    case FWC_CODEC_ERROR:
        return "CODEC_ERROR";
    default:
        assert(false);
    }
}

static void fakewire_dec_raw_data(fw_decoder_t *fwd, uint8_t *bytes_in, size_t byte_count) {
    assert(fwd != NULL && fwd->output != NULL);
    assert(byte_count > 0);
    // if currently processing a parametrized control character, decode the bytes in question
    if (fwd->recv_current != FWC_NONE) {
        assert(fakewire_is_parametrized(fwd->recv_current));
        assert(fwd->recv_count < sizeof(fwd->recv_param));
        size_t count = byte_count;
        uint8_t *buf = (uint8_t*) &fwd->recv_param;
        if (count + fwd->recv_count > sizeof(fwd->recv_param)) {
            count = sizeof(fwd->recv_param) - fwd->recv_count;
        }
        memcpy(buf + fwd->recv_count, bytes_in, count);
        fwd->recv_count += count;
        byte_count -= count;
        bytes_in += count;
        if (fwd->recv_count == sizeof(fwd->recv_param)) {
            fwd->output->recv_ctrl(fwd->output->param, fwd->recv_current, be32toh(fwd->recv_param));
            fwd->recv_current = FWC_NONE;
        }
    }
    // report remaining bytes as-is
    if (byte_count > 0) {
        fwd->output->recv_data(fwd->output->param, bytes_in, byte_count);
    }
}

static void fakewire_dec_raw_ctrl(fw_decoder_t *fwd, fw_ctrl_t symbol) {
    assert(fwd != NULL && fwd->output != NULL);
    // if we receive another control character while still working on a parameter, report it as a codec error.
    if (fwd->recv_current != FWC_NONE) {
        assert(fakewire_is_parametrized(fwd->recv_current));
        debugf("[fakewire_codec] Encountered unexpected control character while decoding parameterized control character %s.",
               fakewire_codec_symbol(symbol));
        fwd->recv_current = FWC_NONE;
        fwd->output->recv_ctrl(fwd->output->param, FWC_CODEC_ERROR, 0);
    }
    // otherwise, if we receive a parameterized control character, start reading the parameter.
    if (fakewire_is_parametrized(symbol)) {
        fwd->recv_current = symbol;
        fwd->recv_count = 0;
    } else {
        // and if we receive a non-parameterized control character, report it directly.
        fwd->output->recv_ctrl(fwd->output->param, symbol, 0);
    }
}

void fakewire_dec_decode(fw_decoder_t *fwd, uint8_t *bytes_in, size_t byte_count) {
    assert(byte_count > 0);

    uint8_t databuf[256];
    size_t db_index = 0;

    for (size_t byte_i = 0; byte_i < byte_count; byte_i++) {
        uint8_t cur_byte = bytes_in[byte_i];

        fw_ctrl_t ctrl_char = FWC_NONE;
        bool consumed = false, is_decoded = false;

        if (fwd->in_escape) {
            fwd->in_escape = false;
            uint8_t decoded = cur_byte ^ 0x10;
            if (fakewire_is_special(decoded)) {
                // valid escape sequence
                cur_byte = decoded;
                is_decoded = true;
            } else {
                // invalid escape sequence; pass the escape up the line for error handling
                ctrl_char = FWC_ESCAPE_SYM;
            }
        }
        if (!is_decoded && fakewire_is_special(cur_byte)) {
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
            fakewire_dec_raw_data(fwd, databuf, db_index);
            db_index = 0;
        }
        // transmit control characters
        if (ctrl_char != FWC_NONE) {
            fakewire_dec_raw_ctrl(fwd, ctrl_char);
        }
        // append new data to buffer
        if (!consumed) {
            databuf[db_index++] = cur_byte;
        }
    }

    // if any data remains, push it to the receiver.
    if (db_index > 0) {
        fakewire_dec_raw_data(fwd, databuf, db_index);
    }
}

void fakewire_enc_init(fw_encoder_t *fwe, ringbuf_t *output) {
    assert(fwe != NULL && output != NULL);
    assert(ringbuf_elem_size(output) == 1);
    fwe->output = output;
}

int fakewire_enc_encode_data(fw_encoder_t *fwe, uint8_t *bytes_in, size_t byte_count) {
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
#ifdef DEBUG
    debugf("[fakewire_codec] Encoded %zu raw data bytes to %zu line bytes.", byte_count, j);
#endif
    int err = ringbuf_write_all(fwe->output, temp, j);
#ifdef DEBUG
    debugf("[fakewire_codec] Completed write of %zu bytes to ring buffer.", j);
#endif
    return err;
}

int fakewire_enc_encode_ctrl(fw_encoder_t *fwe, fw_ctrl_t symbol, uint32_t param) {
    assert(fwe != NULL);
    assert(fakewire_is_special(symbol) && symbol != FWC_ESCAPE_SYM);
    assert(param == 0 || fakewire_is_parametrized(symbol));

    uint8_t byte = symbol;
    if (ringbuf_write_all(fwe->output, &byte, 1) < 0) {
        return -1;
    }
    if (fakewire_is_parametrized(symbol)) {
        uint32_t netparam = htobe32(param);
        if (fakewire_enc_encode_data(fwe, (uint8_t*) &netparam, sizeof(netparam)) < 0) {
            return -1;
        }
    }
    return 0;
}
