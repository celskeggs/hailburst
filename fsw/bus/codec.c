#include <endian.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <hal/debug.h>
#include <bus/codec.h>

//#define CODEC_DEBUG

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

void fakewire_dec_reset(fw_decoder_t *fwd) {
    assert(fwd != NULL);
    // when ducts are used as streams, there is no need to separate their elements.
    assert(duct_max_flow(fwd->rx_duct) == 1);
    assert(duct_message_size(fwd->rx_duct) == fwd->rx_buffer_capacity);
    fwd->rx_length = fwd->rx_offset = 0;
    fwd->rx_timestamp = 0;
    fwd->recv_in_escape = false;
    fwd->recv_current = FWC_NONE;
    fwd->recv_count = 0;
    fwd->recv_param = 0;
    fwd->recv_timestamp_ns = 0;
}

void fakewire_dec_prepare(fw_decoder_t *fwd) {
    assert(fwd != NULL);
    duct_txn_t txn;
    duct_receive_prepare(&txn, fwd->rx_duct, fwd->rx_duct_replica);
    fwd->rx_length = duct_receive_message(&txn, fwd->rx_buffer, &fwd->rx_timestamp);
    fwd->rx_offset = 0;
#ifdef CODEC_DEBUG
    debugf(TRACE, "Decoder received %zu bytes from line.", fwd->rx_length);
#endif
    assert(fwd->rx_length <= fwd->rx_buffer_capacity);
    duct_receive_commit(&txn);
}

void fakewire_dec_commit(fw_decoder_t *fwd) {
    assert(fwd != NULL);
    assert(fwd->rx_offset == fwd->rx_length);
}

// partial version of decode that does not decode control character parameters (ctrl_param is not set)
static bool fakewire_dec_internal_decode(fw_decoder_t *fwd, fw_decoded_ent_t *decoded) {
    assert(fwd != NULL && decoded != NULL);
    assert((decoded->data_max_len > 0) == (decoded->data_out != NULL));

    decoded->ctrl_out = FWC_NONE;
    decoded->data_actual_len = 0;
    decoded->receive_timestamp = fwd->rx_timestamp;

    for (;;) {
        if (fwd->rx_offset == fwd->rx_length) {
            return (decoded->data_actual_len > 0);
        }
        assert(fwd->rx_length >= 1 && fwd->rx_length <= fwd->rx_buffer_capacity);
        assert(fwd->rx_offset < fwd->rx_length);
        assert(decoded->data_out == NULL || decoded->data_actual_len < decoded->data_max_len);

        uint8_t cur_byte = fwd->rx_buffer[fwd->rx_offset++];

        if (fwd->recv_in_escape) {
            uint8_t decoded_byte = cur_byte ^ 0x10;
            if (!fakewire_is_special(decoded_byte)) {
                // invalid escape sequence; pass the escape up the line for error handling
                if (decoded->data_actual_len > 0) {
                    // except... we have data to communicate first!
                    fwd->rx_offset--; // make sure we interpret this byte again
                    return true;
                }
                decoded->ctrl_out = FWC_ESCAPE_SYM;
                fwd->rx_offset--; // don't consume this byte; re-interpret it.
                fwd->recv_in_escape = false; // but without the escape
                return true;
            }
            fwd->recv_in_escape = false;
            // valid escape sequence; write to buffer
            if (decoded->data_out != NULL) {
                decoded->data_out[decoded->data_actual_len++] = decoded_byte;
            } else {
                decoded->data_actual_len++; // blindly increment
            }
        } else if (cur_byte == FWC_ESCAPE_SYM) {
            // handle escape sequence for next byte
            fwd->recv_in_escape = true;
        } else if (fakewire_is_special(cur_byte)) {
            // pass control character up the line
            if (decoded->data_actual_len > 0) {
                // except... we have data to communicate first!
                fwd->rx_offset--; // make sure we interpret this byte again
                return true;
            }
            decoded->ctrl_out = cur_byte;
            return true;
        } else {
            if (decoded->data_out != NULL) {
                decoded->data_out[decoded->data_actual_len++] = cur_byte;
            } else {
                decoded->data_actual_len++; // blindly increment
            }
        }

        if (decoded->data_out != NULL && decoded->data_actual_len == decoded->data_max_len) {
            return true;
        }
    }
}

bool fakewire_dec_decode(fw_decoder_t *fwd, fw_decoded_ent_t *decoded) {
    assert(fwd != NULL && decoded != NULL);
    assert((decoded->data_max_len > 0) == (decoded->data_out != NULL));

    decoded->ctrl_param = 0;

    // primary processing path for non-parameterized control characters and regular data bytes
    if (fwd->recv_current == FWC_NONE) {
        if (!fakewire_dec_internal_decode(fwd, decoded)) {
            return false;
        }

        if (decoded->ctrl_out != FWC_NONE) {
            assert(decoded->data_actual_len == 0);
            // if we receive a non-parameterized control character, return it directly.
            if (!fakewire_is_parametrized(decoded->ctrl_out)) {
#ifdef CODEC_DEBUG
                debugf(TRACE, "Decoded non-parameterized control character: %s.",
                       fakewire_codec_symbol(decoded->ctrl_out));
#endif
                return true;
            }
            // but if it's parameterized, start reading the parameter
            fwd->recv_current = decoded->ctrl_out;
            fwd->recv_count = 0;
            fwd->recv_timestamp_ns = decoded->receive_timestamp;
        } else {
            assertf(decoded->data_actual_len > 0 &&
                        (decoded->data_out == NULL || decoded->data_actual_len <= decoded->data_max_len),
                "data_actual_len=%zu, data_out=%p, data_max_len=%zu",
                decoded->data_actual_len, decoded->data_out, decoded->data_max_len);
            // if we receive a sequence of bytes when not reading a parameter, return them directly.
#ifdef CODEC_DEBUG
            debugf(TRACE, "Decoded sequence of %zu data bytes.", decoded->data_actual_len);
#endif
            return true;
        }
    }

    // secondary processing path for control character parameters
    for (;;) {
        assert(fwd->recv_current != FWC_NONE && fakewire_is_parametrized(fwd->recv_current));
        assert(fwd->recv_count < sizeof(fwd->recv_param));

        fw_decoded_ent_t subdec = {
            .data_out     = ((uint8_t*) &fwd->recv_param) + fwd->recv_count,
            .data_max_len = sizeof(fwd->recv_param) - fwd->recv_count,
        };

        if (!fakewire_dec_internal_decode(fwd, &subdec)) {
            return false;
        }

        if (subdec.ctrl_out != FWC_NONE) {
            assert(subdec.data_actual_len == 0);
            // if we receive another control character while still working on a parameter, report it as a codec error.
            assert(fakewire_is_parametrized(fwd->recv_current));
            debugf(WARNING, "Decoder encountered unexpected control character %s while decoding parameterized control "
                   "character %s.", fakewire_codec_symbol(subdec.ctrl_out), fakewire_codec_symbol(fwd->recv_current));
            decoded->ctrl_out = FWC_CODEC_ERROR;
            decoded->ctrl_param = 0;
            decoded->data_actual_len = 0;
            decoded->receive_timestamp = subdec.receive_timestamp;
            fwd->recv_current = FWC_NONE;
            return true;
        }
        assert(subdec.data_actual_len > 0 && subdec.data_actual_len <= subdec.data_max_len);

        // we're currently processing a parametrized control character, so decode the bytes in question
        fwd->recv_count += subdec.data_actual_len;
        if (fwd->recv_count == sizeof(fwd->recv_param)) {
            decoded->ctrl_out = fwd->recv_current;
            decoded->ctrl_param = be32toh(fwd->recv_param);
            decoded->data_actual_len = 0;
            decoded->receive_timestamp = fwd->recv_timestamp_ns;
            fwd->recv_current = FWC_NONE;
#ifdef CODEC_DEBUG
            debugf(TRACE, "Decoded parameterized control character: %s(0x%08x).",
                   fakewire_codec_symbol(decoded->ctrl_out), decoded->ctrl_param);
#endif
            return true;
        }

        // we didn't get enough bytes, but we don't know whether it was because we ran out of data or because we ran
        // into a unexpected control character. we'll need to go back around to find out.
    }
}

void fakewire_enc_prepare(fw_encoder_t *fwe) {
    assert(fwe != NULL);
    assert(duct_max_flow(fwe->tx_duct) == 1);
    assert(fwe->tx_capacity == duct_message_size(fwe->tx_duct));
    fwe->tx_offset = 0;
}

void fakewire_enc_commit(fw_encoder_t *fwe) {
    assert(fwe != NULL);
    duct_txn_t txn;
    duct_send_prepare(&txn, fwe->tx_duct, fwe->tx_duct_replica);
    if (fwe->tx_offset > 0) {
        duct_send_message(&txn, fwe->tx_buffer, fwe->tx_offset, 0);
#ifdef CODEC_DEBUG
        debugf(TRACE, "Encoder wrote %zu line bytes in commit.", fwe->tx_offset);
#endif
    }
    duct_send_commit(&txn);
}

size_t fakewire_enc_encode_data(fw_encoder_t *fwe, const uint8_t *bytes_in, size_t byte_count) {
    assert(fwe != NULL && bytes_in != NULL);
    assert(byte_count > 0);

    size_t in_offset;
    for (in_offset = 0; in_offset < byte_count; in_offset++) {
        uint8_t byte = bytes_in[in_offset];

        if (fakewire_is_special(byte)) {
            if (fwe->tx_offset + 2 > fwe->tx_capacity) {
                break;
            }
            fwe->tx_buffer[fwe->tx_offset++] = FWC_ESCAPE_SYM;
            // encode byte so that it remains in the data range
            byte ^= 0x10;
        } else {
            if (fwe->tx_offset + 1 > fwe->tx_capacity) {
                break;
            }
        }
        fwe->tx_buffer[fwe->tx_offset++] = byte;
    }
#ifdef CODEC_DEBUG
    debugf(TRACE, "Encoded %zu/%zu raw data bytes.", in_offset, byte_count);
#endif
    return in_offset;
}

bool fakewire_enc_encode_ctrl(fw_encoder_t *fwe, fw_ctrl_t symbol, uint32_t param) {
    assert(fwe != NULL);
    assert(fakewire_is_special(symbol) && symbol != FWC_ESCAPE_SYM);
    assert(param == 0 || fakewire_is_parametrized(symbol));

    // if our buffer fills up, drain it to the output
    if (fwe->tx_offset + (fakewire_is_parametrized(symbol) ? 9 : 1) > fwe->tx_capacity) {
        return false;
    }

    fwe->tx_buffer[fwe->tx_offset++] = (uint8_t) symbol;
    if (fakewire_is_parametrized(symbol)) {
        uint32_t netparam = htobe32(param);
        size_t actual = fakewire_enc_encode_data(fwe, (uint8_t*) &netparam, sizeof(netparam));
        // should always succeed because of reserved space
        assert(actual == sizeof(netparam));
    }

#ifdef CODEC_DEBUG
    debugf(TRACE, "Encoded control character: %s(0x%08x).", fakewire_codec_symbol(symbol), param);
#endif

    return true;
}
