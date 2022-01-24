#include <endian.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <fsw/debug.h>
#include <bus/codec.h>

//#define DEBUG

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

void fakewire_dec_init(fw_decoder_t *fwd, chart_t *rx_chart) {
    assert(fwd != NULL && rx_chart != NULL);
    // clear everything and populate rx_chart
    memset(fwd, 0, sizeof(*fwd));
    fwd->rx_chart = rx_chart;
}

// partial version of decode that does not decode control character parameters (ctrl_param is not set)
static bool fakewire_dec_internal_decode(fw_decoder_t *fwd, fw_decoded_ent_t *decoded) {
    assert(fwd != NULL && decoded != NULL);
    assert((decoded->data_max_len > 0) == (decoded->data_out != NULL));

    decoded->ctrl_out = FWC_NONE;
    decoded->data_actual_len = 0;
    decoded->receive_timestamp = 0;

    for (;;) {
        if (fwd->rx_entry != NULL && fwd->rx_offset == fwd->rx_entry->actual_length) {
            chart_reply_send(fwd->rx_chart, 1);
            fwd->rx_entry = NULL;
        }
        if (fwd->rx_entry == NULL) {
            fwd->rx_entry = chart_reply_start(fwd->rx_chart);
            if (fwd->rx_entry == NULL) {
                return (decoded->data_actual_len > 0);
            }
            fwd->rx_offset = 0;
        }
        assert(fwd->rx_entry->actual_length <= io_rx_size(fwd->rx_chart));
        assert(fwd->rx_offset < fwd->rx_entry->actual_length);
        assert(decoded->data_out == NULL || decoded->data_actual_len < decoded->data_max_len);
        if (decoded->receive_timestamp == 0) {
            decoded->receive_timestamp = fwd->rx_entry->receive_timestamp;
        }

        uint8_t cur_byte = fwd->rx_entry->data[fwd->rx_offset++];

        if (fwd->recv_in_escape) {
            uint8_t decoded_byte = cur_byte ^ 0x10;
            if (!fakewire_is_special(decoded_byte)) {
                // invalid escape sequence; pass the escape up the line for error handling
                if (decoded->data_actual_len > 0) {
                    // except... we have data to communcate first!
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
                return true;
            }
            // but if it's parameterized, start reading the parameter
            fwd->recv_current = decoded->ctrl_out;
            fwd->recv_count = 0;
            fwd->recv_timestamp_ns = decoded->receive_timestamp;
        } else {
            assert(decoded->data_actual_len > 0 && decoded->data_actual_len <= decoded->data_max_len);
            // if we receive a sequence of bytes when not reading a parameter, return them directly.
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
            debugf(CRITICAL, "Encountered unexpected control character %s while decoding parameterized control "
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
            return true;
        }

        // we didn't get enough bytes, but we don't know whether it was because we ran out of data or because we ran
        // into a unexpected control character. we'll need to go back around to find out.
    }
}

void fakewire_enc_init(fw_encoder_t *fwe, chart_t *tx_chart) {
    assert(fwe != NULL && tx_chart != NULL);
    fwe->tx_chart = tx_chart;
    fwe->tx_entry = NULL;
    fwe->is_flush_worthwhile = false;
}

static inline size_t fakewire_enc_space(fw_encoder_t *fwe) {
    if (fwe->tx_entry == NULL) {
        return 0;
    }
    assert(fwe->tx_entry->actual_length <= io_tx_size(fwe->tx_chart));
    return io_tx_size(fwe->tx_chart) - fwe->tx_entry->actual_length;
}

static bool fakewire_enc_pump(fw_encoder_t *fwe) {
    if (fwe->tx_entry == NULL) {
        fwe->tx_entry = chart_request_start(fwe->tx_chart);
        if (fwe->tx_entry != NULL) {
            fwe->tx_entry->actual_length = 0;
            fwe->is_flush_worthwhile = false;
        }
    }
    return fwe->tx_entry != NULL;
}

// does not respect is_flush_worthwhile
static void fakewire_enc_flush_all(fw_encoder_t *fwe) {
    if (fwe->tx_entry != NULL && fwe->tx_entry->actual_length > 0) {
        // transmit buffer entry
        chart_request_send(fwe->tx_chart, 1);
        fwe->tx_entry = NULL;
#ifdef DEBUG
        debugf(TRACE, "Wrote %zu line bytes in flush.", fwe->enc_idx);
#endif
    }
}

static bool fakewire_enc_ensure_space(fw_encoder_t *fwe, size_t min_space) {
    assert(min_space > 0);
    if (fakewire_enc_space(fwe) < min_space) {
        fakewire_enc_flush_all(fwe);
        assert(fwe->tx_entry == NULL || fwe->tx_entry->actual_length == 0);
        if (!fakewire_enc_pump(fwe)) {
            return false;
        }
    }
    assert(fwe->tx_entry != NULL);
    assertf(fakewire_enc_space(fwe) >= min_space,
            "not enough space: %zu < %zu", fakewire_enc_space(fwe), min_space);
    return true;
}

size_t fakewire_enc_encode_data(fw_encoder_t *fwe, const uint8_t *bytes_in, size_t byte_count) {
    assert(fwe != NULL && bytes_in != NULL);
    assert(byte_count > 0);

#ifdef DEBUG
    debugf(TRACE, "Beginning encoding of %zu raw data bytes.", byte_count);
#endif
    size_t in_offset;
    for (in_offset = 0; in_offset < byte_count; in_offset++) {
        uint8_t byte = bytes_in[in_offset];

        if (!fakewire_enc_ensure_space(fwe, fakewire_is_special(byte) ? 2 : 1)) {
            break;
        }

        if (fakewire_is_special(byte)) {
            fwe->tx_entry->data[fwe->tx_entry->actual_length++] = FWC_ESCAPE_SYM;
            // encode byte so that it remains in the data range
            byte ^= 0x10;
        }
        fwe->tx_entry->data[fwe->tx_entry->actual_length++] = byte;
    }
#ifdef DEBUG
    debugf(TRACE, "Finished encoding %zu/%zu raw data bytes.", in_offset, byte_count);
#endif
    return in_offset;
}

bool fakewire_enc_encode_ctrl(fw_encoder_t *fwe, fw_ctrl_t symbol, uint32_t param) {
    assert(fwe != NULL);
    assert(fakewire_is_special(symbol) && symbol != FWC_ESCAPE_SYM);
    assert(param == 0 || fakewire_is_parametrized(symbol));

    // if our buffer fills up, drain it to the output
    if (!fakewire_enc_ensure_space(fwe, fakewire_is_parametrized(symbol) ? 9 : 1)) {
        return false;
    }

#ifdef DEBUG
    debugf(TRACE, "Transmitting control character: %s(%u).", fakewire_codec_symbol(symbol), param);
#endif

    fwe->tx_entry->data[fwe->tx_entry->actual_length++] = (uint8_t) symbol;
    if (fakewire_is_parametrized(symbol)) {
        uint32_t netparam = htobe32(param);
        size_t actual = fakewire_enc_encode_data(fwe, (uint8_t*) &netparam, sizeof(netparam));
        // should always succeed because of reserved space
        assert(actual == sizeof(netparam));
    }

    if (symbol != FWC_START_PACKET) {
        fwe->is_flush_worthwhile = true;
    }

    return true;
}

void fakewire_enc_flush(fw_encoder_t *fwe) {
    if (fwe->is_flush_worthwhile) {
        fakewire_enc_flush_all(fwe);
    }
}
