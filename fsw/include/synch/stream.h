#ifndef FSW_STREAM_H
#define FSW_STREAM_H

#include <stdint.h>

#include <hal/thread.h>

// stream implementation based on the "good option" from here:
// https://www.snellman.net/blog/archive/2016-12-13-ring-buffers/

typedef struct {
    thread_t writer;
    thread_t reader;

    uint8_t *memory;
    size_t   capacity;
    // TODO: make sure I test integer overflow or read_idx and write_idx... SHOULD be fine, but needs to be tested
    size_t   read_idx;
    size_t   write_idx;
} stream_t;

#define STREAM_REGISTER(s_ident, s_capacity)                                                    \
    static_assert(((s_capacity) & ((s_capacity) - 1)) == 0, "capacity must be a power of two"); \
    static_assert(((s_capacity) << 1) != 0, "capacity must leave at least one bit free");       \
    static uint8_t s_ident ## _memory[s_capacity];                                              \
    stream_t s_ident = {                                                                        \
        .writer = NULL, /* to be populated later */                                             \
        .reader = NULL, /* to be populated later */                                             \
        .memory = s_ident ## _memory,                                                           \
        .capacity = (s_capacity),                                                               \
        .read_idx = 0,                                                                          \
        .write_idx = 0,                                                                         \
    }

void stream_set_writer(stream_t *stream, thread_t writer);
void stream_set_reader(stream_t *stream, thread_t reader);
// may only be used by a single thread at a time
void stream_write(stream_t *stream, uint8_t *data, size_t length);
// may only be used by a single thread at a time
size_t stream_read(stream_t *stream, uint8_t *data, size_t max_len);

#endif /* FSW_STREAM_H */
