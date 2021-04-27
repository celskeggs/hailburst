#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bitbuf.h"

/*
typedef struct bit_buf_st {
    size_t start_avail_bits;
    size_t end_avail_bytes;
    size_t capacity;
    uint8_t *buffer;
} bit_buf_t;
*/

void bit_buf_init(bit_buf_t *bb, size_t capacity) {
    assert(bb != NULL);
    assert(capacity >= 1);
    bb->start_avail_bits = 0;
    bb->end_avail_bytes = 0;
    bb->capacity = capacity;
    bb->buffer = malloc(capacity);
    if (bb->buffer == NULL) {
        perror("malloc");
        exit(1);
    }
    memset(bb->buffer, 0, capacity);
}

void bit_buf_destroy(bit_buf_t *bb) {
    assert(bb != NULL && bb->buffer != NULL);
    free(bb->buffer);
    bb->buffer = NULL;
    memset(bb, 0, sizeof(bit_buf_t));
}

size_t bit_buf_insertable_bytes(bit_buf_t *bb) {
    assert(bb->start_avail_bits <= bb->end_avail_bytes * 8);
    assert(bb->end_avail_bytes <= bb->capacity);
    return bb->capacity - bb->end_avail_bytes + bb->start_avail_bits / 8;
}

static void bit_buf_compact(bit_buf_t *bb) {
    size_t shift = bb->start_avail_bits / 8;
    assert(shift >= 1);
    memmove(bb->buffer, bb->buffer + shift, bb->end_avail_bytes - shift);
    bb->start_avail_bits -= shift * 8;
    bb->end_avail_bytes -= shift;
    assert(bb->start_avail_bits < 8);
}

void bit_buf_insert_bytes(bit_buf_t *bb, uint8_t *buffer, size_t add_count) {
    if (add_count > bb->capacity - bb->end_avail_bytes) {
        bit_buf_compact(bb);
    }
    assert(add_count <= bb->capacity - bb->end_avail_bytes);
    memcpy(&bb->buffer[bb->end_avail_bytes], buffer, add_count);
    bb->end_avail_bytes += add_count;
}

static void bitcopy(uint8_t *dest, int dest_offset_bits, uint8_t *src, int src_offset_bits, int count_bits) {
    assert(0 <= dest_offset_bits);
    assert(0 <= src_offset_bits);
    for (int i = 0; i < count_bits; i++) {
        int bitval = (src[src_offset_bits / 8] >> (src_offset_bits % 8)) & 1;
        dest[dest_offset_bits / 8] =
                ((dest[dest_offset_bits / 8] & ~(1 << (dest_offset_bits % 8)))
                 | (bitval << (dest_offset_bits % 8)));
        dest_offset_bits++;
        src_offset_bits++;
    }
}

size_t bit_buf_extractable_bits(bit_buf_t *bb) {
    assert(bb->end_avail_bytes * 8 >= bb->start_avail_bits);
    return bb->end_avail_bytes * 8 - bb->start_avail_bits;
}

uint32_t bit_buf_peek_bits(bit_buf_t *bb, size_t bits) {
    assert(bits <= bit_buf_extractable_bits(bb));
    assert(bits <= 32);
    uint8_t out[4] = {0, 0, 0, 0};
    bitcopy(out, 0, bb->buffer, bb->start_avail_bits, bits);
    return out[0] | (out[1] << 8) | (out[2] << 16) | (out[3] << 24);
}

uint32_t bit_buf_extract_bits(bit_buf_t *bb, size_t bits) {
    uint32_t result = bit_buf_peek_bits(bb, bits);
    bb->start_avail_bits += bits;
    assert(bb->start_avail_bits <= 8 * bb->end_avail_bytes);
    return result;
}
