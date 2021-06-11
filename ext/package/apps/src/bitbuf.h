#ifndef APP_BITBUF_H
#define APP_BITBUF_H

#include <stddef.h>
#include <stdint.h>

// Bit buffer structure

typedef struct bit_buf_st {
    size_t start_avail_bits;
    size_t end_avail_bytes;
    size_t capacity;
    uint8_t *buffer;
} bit_buf_t;

void bit_buf_init(bit_buf_t *bb, size_t capacity);
void bit_buf_destroy(bit_buf_t *bb);

size_t bit_buf_insertable_bytes(bit_buf_t *bb);
void bit_buf_insert_bytes(bit_buf_t *bb, uint8_t *buffer, size_t add_count);

size_t bit_buf_extractable_bits(bit_buf_t *bb);
uint32_t bit_buf_extract_bits(bit_buf_t *bb, size_t bits);
uint32_t bit_buf_peek_bits(bit_buf_t *bb, size_t bits);

#endif /* APP_BITBUF_H */
