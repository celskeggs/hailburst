#include <arpa/inet.h>
#include <assert.h>
#include <stdint.h>
#include <zlib.h> // for crc32

#include "comm.h"

enum {
    COMM_SCRATCH_SIZE   = 1024,
    COMM_CMD_MAGIC_NUM  = 0x73133C2C, // "tele-exec"

    // internal representations to be returned by comm_dec_next_symbol
    SYMBOL_PACKET_START = 0x1000,
    SYMBOL_PACKET_END   = 0x2000,
    SYMBOL_ERROR        = 0x3000,
};

void comm_dec_init(comm_dec_t *dec, ringbuf_t *uplink) {
    assert(dec != NULL && uplink != NULL);
    assert(ringbuf_elem_size(uplink) == 1);
    dec->uplink = uplink;
    dec->scratch_buffer = malloc(COMM_SCRATCH_SIZE);
    dec->resume_start = dec->resume_end = 0;
    dec->err_count = 0;
}

// The range [0, protect_len) is reserved for use by the decoder, and therefore must not be touched by this function.
static uint8_t comm_dec_next_byte(comm_dec_t *dec, size_t protect_len) {
    assert(dec != NULL && protect_len < COMM_SCRATCH_SIZE);
    // if we don't have enough bytes for the next symbol, we need to get more.
    if (dec->resume_start == dec->resume_end) {
        uint8_t *region = dec->scratch_buffer + protect_len;
        size_t count = ringbuf_read(dec->uplink, region, COMM_SCRATCH_SIZE - protect_len, RB_BLOCKING);
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
    if (next_byte != 0xFF) {
        return next_byte;
    }
    // if 0xFF, then we need to grab another byte to complete the escape sequence
    switch (comm_dec_next_byte(dec, protect_len)) {
    case 0x11:
        return 0xFF;
    case 0x22:
        return SYMBOL_PACKET_START;
    case 0x33:
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
    uint32_t magic_number = ntohl(*(uint32_t*) (buffer + 0));
    uint32_t command_id   = ntohl(*(uint32_t*) (buffer + 4));
    uint32_t timestamp_hl = ntohl(*(uint32_t*) (buffer + 8));
    uint32_t timestamp_ll = ntohl(*(uint32_t*) (buffer + 12));
    if (magic_number != COMM_CMD_MAGIC_NUM) {
        return false;
    }
    // check the CRC32
    uint32_t header_crc32   = ntohl(*(uint32_t*) (buffer + length - 4));
    uint32_t computed_crc32 = crc32(0, buffer, length - 4);
    if (header_crc32 != computed_crc32) {
        return false;
    }
    out->cmd_tlm_id = command_id;
    out->timestamp_ns = (((uint64_t) timestamp_hl) << 32) | timestamp_ll;
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
