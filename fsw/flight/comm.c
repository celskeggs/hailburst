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

    BYTE_ESCAPE     = 0xFF,
    BYTE_ESC_ESCAPE = 0x11,
    BYTE_ESC_SOP    = 0x22,
    BYTE_ESC_EOP    = 0x33,
};

void comm_dec_init(comm_dec_t *dec, stream_t *uplink) {
    assert(dec != NULL && uplink != NULL);
    dec->uplink = uplink;
    dec->resume_start = dec->resume_end = 0;
    dec->err_count = 0;
}

void comm_dec_set_task(comm_dec_t *dec, thread_t thread) {
    assert(dec != NULL);
    stream_set_reader(dec->uplink, thread);
}

// The range [0, protect_len) is reserved for use by the decoder, and therefore must not be touched by this function.
static uint8_t comm_dec_next_byte(comm_dec_t *dec, size_t protect_len) {
    assert(dec != NULL && protect_len < COMM_SCRATCH_SIZE);
    // if we don't have enough bytes for the next symbol, we need to get more.
    if (dec->resume_start == dec->resume_end) {
        uint8_t *region = dec->scratch_buffer + protect_len;
        size_t count = stream_read(dec->uplink, region, COMM_SCRATCH_SIZE - protect_len, true);
        assert(count > 0 && count + protect_len <= COMM_SCRATCH_SIZE);
        dec->resume_start = protect_len;
        dec->resume_end = protect_len + count;
    }
    // check invariants
    assert(dec->resume_end > dec->resume_start);  // proper ordering
    assert(dec->resume_start >= protect_len);     // make sure the protected space is respected
    // return next byte
    return dec->scratch_buffer[dec->resume_start++];
}

static uint16_t comm_dec_next_symbol(comm_dec_t *dec, size_t protect_len) {
    uint8_t next_byte = comm_dec_next_byte(dec, protect_len);
    if (next_byte != BYTE_ESCAPE) {
        return next_byte;
    }
    // if BYTE_ESCAPE, then we need to grab another byte to complete the escape sequence
    switch (comm_dec_next_byte(dec, protect_len)) {
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

// NOTE: the byte array produced here will be reused on the next call
void comm_dec_decode(comm_dec_t *dec, comm_packet_t *out) {
    uint16_t symbol = comm_dec_next_symbol(dec, 0);
    for (;;) {
        // first, scan forward until we get a packet start
        while (symbol != SYMBOL_PACKET_START) {
            dec->err_count += 1; // because any filler bytes before the packet are erroneous
            symbol = comm_dec_next_symbol(dec, 0);
        }
        // now that we've gotten the start symbol, we need to decode the body of the packet
        size_t byte_count = 0;
        symbol = comm_dec_next_symbol(dec, byte_count);
        while (byte_count < COMM_SCRATCH_SIZE - 1 && symbol <= 0xFF) {
            dec->scratch_buffer[byte_count++] = symbol;
            symbol = comm_dec_next_symbol(dec, byte_count);
        }
        if (symbol == SYMBOL_PACKET_END) {
            // now see if we just decoded a valid packet...
            if (comm_packet_decode(out, dec->scratch_buffer, byte_count)) {
                // we did! return it.
                return;
            }
            // otherwise, we didn't.
        }
        // otherwise... we simply don't have a valid packet. we must discard this one and try again.
        dec->err_count += 1;
    }
}

void comm_enc_init(comm_enc_t *enc, stream_t *downlink) {
    assert(enc != NULL && downlink != NULL);
    enc->downlink = downlink;
}

void comm_enc_set_task(comm_enc_t *enc, thread_t thread) {
    assert(enc != NULL);
    stream_set_writer(enc->downlink, thread);
}

static void comm_enc_escape_limited(comm_enc_t *enc, const uint8_t *data, size_t len) {
    assert(0 < len && len <= COMM_SCRATCH_SIZE / 2);
    size_t out_i = 0;
    for (size_t in_i = 0; in_i < len; in_i++) {
        assert(out_i < COMM_SCRATCH_SIZE - 1);
        uint8_t di = data[in_i];
        enc->scratch_buffer[out_i++] = di;
        if (di == BYTE_ESCAPE) {
            enc->scratch_buffer[out_i++] = BYTE_ESC_ESCAPE;
        }
    }
    stream_write(enc->downlink, enc->scratch_buffer, out_i);
}

static void comm_enc_escape(comm_enc_t *enc, const uint8_t *data, size_t len) {
    while (len > COMM_SCRATCH_SIZE / 2) {
        comm_enc_escape_limited(enc, data, COMM_SCRATCH_SIZE / 2);
        data += COMM_SCRATCH_SIZE / 2;
        len  -= COMM_SCRATCH_SIZE / 2;
    }
    if (len > 0) {
        comm_enc_escape_limited(enc, data, len);
    }
}

void comm_enc_encode(comm_enc_t *enc, comm_packet_t *in) {
    assert(enc != NULL && in != NULL);

    // start of packet
    stream_write(enc->downlink, (uint8_t[]) {BYTE_ESCAPE, BYTE_ESC_SOP}, 2);

    // encode header fields
    uint32_t fields[] = {
        htobe32(COMM_TLM_MAGIC_NUM),
        htobe32(in->cmd_tlm_id),
        htobe32((uint32_t) (in->timestamp_ns >> 32)),
        htobe32((uint32_t) (in->timestamp_ns >> 0)),
    };
    uint32_t crc;
    comm_enc_escape(enc, (uint8_t*) fields, sizeof(fields));
    crc = crc32(0, (uint8_t*) fields, sizeof(fields));

    // encode body
    comm_enc_escape(enc, in->data_bytes, in->data_len);
    crc = crc32(crc, in->data_bytes, in->data_len);

    // encode trailing CRC
    crc = htobe32(crc);
    comm_enc_escape(enc, (uint8_t*) &crc, sizeof(crc));

    // end of packet
    stream_write(enc->downlink, (uint8_t[]) {BYTE_ESCAPE, BYTE_ESC_EOP}, 2);
}
