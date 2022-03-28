#include <endian.h>
#include <stdint.h>
#include <zlib.h> // for crc32

#include <flight/comm.h>

enum {
    COMM_CMD_MAGIC_NUM  = 0x73133C2C, // "tele-exec"
    COMM_TLM_MAGIC_NUM  = 0x7313DA7A, // "tele-data"

    // internal representations to be returned by comm_dec_next_symbol
    SYMBOL_PACKET_START = 0x1000,
    SYMBOL_PACKET_END   = 0x2000,
    SYMBOL_ERROR        = 0x3000,
    SYMBOL_BUFFER_EMPTY = 0x4000,

    BYTE_ESCAPE     = 0xFF,
    BYTE_ESC_ESCAPE = 0x11,
    BYTE_ESC_SOP    = 0x22,
    BYTE_ESC_EOP    = 0x33,
};

static uint16_t comm_dec_next_symbol(comm_dec_t *dec) {
    // we can only proceed if we either have >=1 byte available (which is not BYTE_ESCAPE) or >=2 bytes available (if
    // the first one is BYTE_ESCAPE)
    if (!pipe_receiver_has_next(dec->uplink, 2) &&
            !(pipe_receiver_has_next(dec->uplink, 1) && pipe_receiver_peek_byte(dec->uplink) != BYTE_ESCAPE)) {
        return SYMBOL_BUFFER_EMPTY;
    }
    uint8_t next_byte = pipe_receiver_read_byte(dec->uplink);
    if (next_byte != BYTE_ESCAPE) {
        return next_byte;
    }
    // if BYTE_ESCAPE, then we need to grab another byte to complete the escape sequence
    switch (pipe_receiver_read_byte(dec->uplink)) {
    case BYTE_ESC_ESCAPE:
        return BYTE_ESCAPE;
    case BYTE_ESC_SOP:
        return SYMBOL_PACKET_START;
    case BYTE_ESC_EOP:
        return SYMBOL_PACKET_END;
    default:
        // invalid escape sequence
        return SYMBOL_ERROR;
    }
}

static bool comm_packet_decode(comm_packet_t *out, uint8_t *buffer, size_t length) {
    // needs to be long enough to have all the fields
    if (length < 4 + 4 + 8 + 4) {
        return false;
    }
    // decode header fields
    uint32_t magic_number = be32toh(*(uint32_t*) (buffer + 0));
    uint32_t command_id   = be32toh(*(uint32_t*) (buffer + 4));
    uint64_t timestamp_ns = be64toh(*(uint64_t*) (buffer + 8));
    if (magic_number != COMM_CMD_MAGIC_NUM) {
        return false;
    }
    // check the CRC32
    uint32_t header_crc32   = be32toh(*(uint32_t*) (buffer + length - 4));
    uint32_t computed_crc32 = crc32(0, buffer, length - 4);
    if (header_crc32 != computed_crc32) {
        return false;
    }
    out->cmd_tlm_id = command_id;
    out->timestamp_ns = timestamp_ns;
    out->data_len = length - 20;
    out->data_bytes = buffer + 16;
    return true;
}

void comm_dec_reset(comm_dec_t *dec) {
    assert(dec != NULL);
    pipe_receiver_reset(dec->uplink);
    dec->decode_in_progress = false;
    dec->decode_offset = 0;
}

void comm_dec_prepare(comm_dec_t *dec) {
    assert(dec != NULL);
    pipe_receiver_prepare(dec->uplink);
    dec->err_count = 0;
}

// NOTE: the byte array produced here will be reused on the next call
bool comm_dec_decode(comm_dec_t *dec, comm_packet_t *out) {
    uint16_t symbol;
    bool success = false;
    while ((symbol = comm_dec_next_symbol(dec)) != SYMBOL_BUFFER_EMPTY) {
        if (!dec->decode_in_progress) {
            if (symbol == SYMBOL_PACKET_START) {
                dec->decode_in_progress = true;
                dec->decode_offset = 0;
            } else {
                dec->err_count++;
            }
        } else {
            if (symbol <= 0xFF) {
                if (dec->decode_offset >= sizeof(dec->decode_buffer)) {
                    dec->decode_in_progress = false;
                    debugf(WARNING, "Comm packet decoder discarded packet of at least %zu bytes; exceeded decode "
                           "buffer size.", dec->decode_offset + 1);
                } else {
                    dec->decode_buffer[dec->decode_offset++] = symbol;
                }
            } else if (symbol == SYMBOL_PACKET_END) {
                dec->decode_in_progress = false;
                if (comm_packet_decode(out, dec->decode_buffer, dec->decode_offset)) {
                    // valid packet!
                    success = true;
                    break;
                }
                debugf(WARNING, "Comm packet of length %zu could not be validated. Discarded.", dec->decode_offset);
            } else {
                dec->decode_in_progress = false;
                debugf(WARNING, "Comm packet of at least length %zu discarded due to unexpected symbol %u.",
                       dec->decode_offset, symbol);
            }
        }
    }
    return success;
}

void comm_dec_commit(comm_dec_t *dec) {
    assert(dec != NULL);
    if (dec->err_count > 0) {
        debugf(WARNING, "Comm packet decoder discarded %u unexpected bytes.", dec->err_count);
    }
    pipe_receiver_commit(dec->uplink);
}

static size_t comm_enc_estimate_length(const uint8_t *data, size_t len) {
    size_t total = len;
    for (size_t i = 0; i < len; i++) {
        if (data[i] == BYTE_ESCAPE) {
            total++;
        }
    }
    return total;
}

static void comm_enc_write_escaped(comm_enc_t *enc, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        uint8_t di = data[i];
        pipe_sender_write_byte(enc->downlink, di);
        if (di == BYTE_ESCAPE) {
            pipe_sender_write_byte(enc->downlink, BYTE_ESC_ESCAPE);
        }
    }
}

void comm_enc_reset(comm_enc_t *enc) {
    assert(enc != NULL);
    pipe_sender_reset(enc->downlink);
}

void comm_enc_prepare(comm_enc_t *enc) {
    assert(enc != NULL);
    pipe_sender_prepare(enc->downlink);
}

bool comm_enc_encode(comm_enc_t *enc, comm_packet_t *in) {
    assert(enc != NULL && in != NULL);

    size_t expected_length = 2 /* for start-of-packet */
                           + sizeof(uint32_t) * 4 * 2 /* maximum size of the header fields encoded */
                           + comm_enc_estimate_length(in->data_bytes, in->data_len) /* body bytes */
                           + sizeof(uint32_t) * 2 /* maximum size of the CRC encoded */
                           + 2;

    if (!pipe_sender_reserve(enc->downlink, expected_length)) {
        return false;
    }

    // start of packet
    pipe_sender_write_byte(enc->downlink, BYTE_ESCAPE);
    pipe_sender_write_byte(enc->downlink, BYTE_ESC_SOP);

    // prepare header fields
    uint32_t fields[] = {
        htobe32(COMM_TLM_MAGIC_NUM),
        htobe32(in->cmd_tlm_id),
        htobe32((uint32_t) (in->timestamp_ns >> 32)),
        htobe32((uint32_t) (in->timestamp_ns >> 0)),
    };
    uint32_t crc;

    // encode header fields
    comm_enc_write_escaped(enc, (uint8_t*) fields, sizeof(fields));
    crc = crc32(0, (uint8_t*) fields, sizeof(fields));

    // encode body
    comm_enc_write_escaped(enc, in->data_bytes, in->data_len);
    crc = crc32(crc, in->data_bytes, in->data_len);

    // encode trailing CRC
    crc = htobe32(crc);
    comm_enc_write_escaped(enc, (uint8_t*) &crc, sizeof(crc));

    // end of packet
    pipe_sender_write_byte(enc->downlink, BYTE_ESCAPE);
    pipe_sender_write_byte(enc->downlink, BYTE_ESC_EOP);

    return true;
}

void comm_enc_commit(comm_enc_t *enc) {
    assert(enc != NULL);
    pipe_sender_commit(enc->downlink);
}
