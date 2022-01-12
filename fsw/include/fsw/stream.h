#ifndef FSW_STREAM_H
#define FSW_STREAM_H

#include <stdbool.h>
#include <stdint.h>

#include <hal/thread.h>

// stream implementation based on the "good option" from here:
// https://www.snellman.net/blog/archive/2016-12-13-ring-buffers/

typedef struct {
    // semaphores to notify when data is ready to be read or written
    semaphore_t unblock_write;
    semaphore_t unblock_read;

    uint8_t *memory;
    size_t   capacity;
    // TODO: make sure I test integer overflow or read_idx and write_idx... SHOULD be fine, but needs to be tested
    size_t   read_idx;
    size_t   write_idx;
} stream_t;

void stream_init(stream_t *stream, size_t capacity);
// may only be used by a single thread at a time
void stream_write(stream_t *stream, uint8_t *data, size_t length);
// may only be used by a single thread at a time
size_t stream_read(stream_t *stream, uint8_t *data, size_t max_len);

#endif /* FSW_STREAM_H */
