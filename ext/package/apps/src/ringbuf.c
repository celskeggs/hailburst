#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ringbuf.h"

void ringbuf_init(ringbuf_t *rb, size_t capacity) {
    mutex_init(&rb->mutex);
    cond_init(&rb->cond);
    // make sure this is a power of two
    assert((capacity & (capacity - 1)) == 0);
    // make sure at least one bit is free
    assert((capacity << 1) != 0);

    rb->memory = malloc(capacity);
    assert(rb->memory != NULL);
    rb->capacity = capacity;
    rb->read_idx = rb->write_idx = 0;
}

// masks an unwrapped index into a valid array offset
static inline size_t mask(ringbuf_t *rb, size_t index) {
    return index & (rb->capacity - 1);
}

// _locked means the function assumes the lock is held
static inline size_t ringbuf_size_locked(ringbuf_t *rb) {
    size_t size = rb->write_idx - rb->read_idx;
    assert(size <= rb->capacity);
    return size;
}

static inline size_t ringbuf_space_locked(ringbuf_t *rb) {
    return rb->capacity - ringbuf_size_locked(rb);
}

size_t ringbuf_write(ringbuf_t *rb, uint8_t *data_in, size_t data_len, ringbuf_flags_t flags) {
    mutex_lock(&rb->mutex);
    // first, if we're being asked to write more data than we can, limit it.
    size_t space = ringbuf_space_locked(rb);
    if (flags & RB_BLOCKING) {
        while (space == 0) {
            cond_wait(&rb->cond, &rb->mutex);
            space = ringbuf_space_locked(rb);
        }
    }
    if (data_len > space) {
        data_len = space;
    }
    if (data_len > 0) {
        // might need up to two writes: a tail write, and a head write.
        size_t tail_write_index = mask(rb, rb->write_idx);
        // first, the tail write
        size_t tail_write_len = data_len;
        if (tail_write_index + tail_write_len > rb->capacity) {
            tail_write_len = rb->capacity - tail_write_index;
        }
        assert(tail_write_len <= data_len);
        memcpy(&rb->memory[tail_write_index], data_in, tail_write_len);
        // then, if necessary, the head write
        size_t head_write_len = data_len - tail_write_len;
        if (head_write_len > 0) {
            memcpy(&rb->memory[0], data_in + tail_write_len, head_write_len);
        }
        rb->write_idx += data_len;
        cond_broadcast(&rb->cond);
    }
    assert(ringbuf_space_locked(rb) + data_len == space);
    mutex_unlock(&rb->mutex);
    return data_len;
}

size_t ringbuf_read(ringbuf_t *rb, uint8_t *data_out, size_t data_len, ringbuf_flags_t flags) {
    mutex_lock(&rb->mutex);
    // first, if we're being asked to read more data than we have, limit it.
    size_t size = ringbuf_size_locked(rb);
    while (size == 0 && (flags & RB_BLOCKING)) {
        cond_wait(&rb->cond, &rb->mutex);
        size = ringbuf_size_locked(rb);
    }
    if (data_len > size) {
        data_len = size;
    }
    if (data_len > 0) {
        // might need up to two reads: a tail read, and a head read.
        size_t tail_read_index = mask(rb, rb->read_idx);
        // first, the tail read
        size_t tail_read_len = data_len;
        if (tail_read_index + tail_read_len > rb->capacity) {
            tail_read_len = rb->capacity - tail_read_index;
        }
        assert(tail_read_len <= data_len);
        memcpy(data_out, &rb->memory[tail_read_index], tail_read_len);
        // then, if necessary, the head read
        size_t head_read_len = data_len - tail_read_len;
        if (head_read_len > 0) {
            memcpy(data_out + tail_read_len, &rb->memory[0], head_read_len);
        }
        rb->read_idx += data_len;
        cond_broadcast(&rb->cond);
    }
    assert(ringbuf_size_locked(rb) + data_len == size);
    mutex_unlock(&rb->mutex);
    return data_len;
}

size_t ringbuf_size(ringbuf_t *rb) {
    mutex_lock(&rb->mutex);
    size_t size = ringbuf_size_locked(rb);
    mutex_unlock(&rb->mutex);
    return size;
}

size_t ringbuf_space(ringbuf_t *rb) {
    mutex_lock(&rb->mutex);
    size_t space = ringbuf_space_locked(rb);
    mutex_unlock(&rb->mutex);
    return space;
}

void ringbuf_write_all(ringbuf_t *rb, uint8_t *data_in, size_t data_len) {
    while (data_len > 0) {
        size_t sent = ringbuf_write(rb, data_in, data_len, RB_BLOCKING);
        assert(sent > 0 && sent <= data_len);
        data_len -= sent;
        data_in += sent;
    }
}
