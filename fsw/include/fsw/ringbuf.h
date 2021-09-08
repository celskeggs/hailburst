#ifndef FSW_RINGBUF_H
#define FSW_RINGBUF_H

#include <stdint.h>

#include <hal/thread.h>

typedef enum {
    RB_NONBLOCKING = 0,
    RB_BLOCKING    = 1,
} ringbuf_flags_t;

// implementation based on the "good option" from here: https://www.snellman.net/blog/archive/2016-12-13-ring-buffers/
typedef struct {
    mutex_t mutex;

    // semaphores to notify when data is ready to be read or written
    semaphore_t unblock_write;
    semaphore_t unblock_read;
    // advisory flags to catch simultaneous blocking writes or simultaneous blocking reads...
    // (also used for optimization purposes)
    bool blocked_write;
    bool blocked_read;

    uint8_t *memory;
    size_t   elem_size;
    size_t   capacity;
    // TODO: make sure I test integer overflow or read_idx and write_idx... SHOULD be fine, but needs to be tested
    size_t   read_idx;
    size_t   write_idx;
} ringbuf_t;

// these two functions are not threadsafe, but the others in this file are.
void ringbuf_init(ringbuf_t *rb, size_t capacity, size_t elem_size);

// warning: only one thread can safely be blocked reading or writing at a time.
size_t ringbuf_write(ringbuf_t *rb, void *data_in, size_t elem_count, ringbuf_flags_t flags);
size_t ringbuf_read(ringbuf_t *rb, void *data_out, size_t elem_count, ringbuf_flags_t flags);

void ringbuf_write_all(ringbuf_t *rb, void *data_in, size_t elem_count);

static inline size_t ringbuf_elem_size(ringbuf_t *rb) {
    return rb->elem_size;
}
static inline size_t ringbuf_capacity(ringbuf_t *rb) {
    return rb->capacity;
}
size_t ringbuf_size(ringbuf_t *rb);
size_t ringbuf_space(ringbuf_t *rb);

#endif /* FSW_RINGBUF_H */
