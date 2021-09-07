#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <fsw/debug.h>
#include <fsw/ringbuf.h>

void ringbuf_init(ringbuf_t *rb, size_t capacity, size_t elem_size) {
    assert(rb != NULL);
    mutex_init(&rb->mutex);
    semaphore_init(&rb->unblock_write);
    semaphore_init(&rb->unblock_read);
    // make sure this is a power of two
    assert((capacity & (capacity - 1)) == 0);
    // make sure at least one bit is free
    assert((capacity << 1) != 0);
    assert(elem_size >= 1);

    rb->memory = malloc(capacity * elem_size);
    assert(rb->memory != NULL);
    rb->capacity = capacity;
    rb->elem_size = elem_size;
    rb->read_idx = rb->write_idx = 0;
    rb->shutdown = false;
}

void ringbuf_shutdown(ringbuf_t *rb) {
    assert(rb != NULL);
    assert(rb->memory != NULL);
    mutex_lock(&rb->mutex);
    assert(rb->shutdown == false);
    rb->shutdown = true;
    semaphore_give(&rb->unblock_read);
    semaphore_give(&rb->unblock_write);
    mutex_unlock(&rb->mutex);
}

void ringbuf_destroy(ringbuf_t *rb) {
    assert(rb != NULL);
    assert(rb->memory != NULL);

    // free memory
    free(rb->memory);
    rb->memory = NULL;

    // tear down pthread objects
    semaphore_destroy(&rb->unblock_write);
    semaphore_destroy(&rb->unblock_read);
    mutex_destroy(&rb->mutex);

    // wipe memory to make use-after-destroy bugs more obvious
    memset(rb, 0, sizeof(ringbuf_t));
}

// masks an unwrapped index into a valid array offset
static inline size_t mask(ringbuf_t *rb, size_t index) {
    assert(rb != NULL);
    return index & (rb->capacity - 1);
}

// _locked means the function assumes the lock is held
static inline size_t ringbuf_size_locked(ringbuf_t *rb) {
    assert(rb != NULL);
    size_t size = rb->write_idx - rb->read_idx;
    assert(size <= rb->capacity);
    return size;
}

static inline size_t ringbuf_space_locked(ringbuf_t *rb) {
    assert(rb != NULL);
    return rb->capacity - ringbuf_size_locked(rb);
}

size_t ringbuf_write(ringbuf_t *rb, void *data_in, size_t elem_count, ringbuf_flags_t flags) {
    assert(rb != NULL && data_in != NULL);
    mutex_lock(&rb->mutex);
    // first, if we're being asked to write more data than we can, limit it.
    size_t space = ringbuf_space_locked(rb);
    if ((flags & RB_BLOCKING) && space == 0) {
        assert(!rb->blocked_write);
        rb->blocked_write = true;

        while (space == 0 && !rb->shutdown) {
            mutex_unlock(&rb->mutex);
            semaphore_take(&rb->unblock_write);
            mutex_lock(&rb->mutex);
            space = ringbuf_space_locked(rb);
        }

        assert(rb->blocked_write == true);
        rb->blocked_write = false;
    }
    if (rb->shutdown) {
        mutex_unlock(&rb->mutex);
        return 0;
    }
    if (elem_count > space) {
        elem_count = space;
    }
    bool wakeup_reader = false;
    if (elem_count > 0) {
        // might need up to two writes: a tail write, and a head write.
        size_t tail_write_index = mask(rb, rb->write_idx);
        // first, the tail write
        size_t tail_write_count = elem_count;
        if (tail_write_index + tail_write_count > rb->capacity) {
            tail_write_count = rb->capacity - tail_write_index;
        }
        assert(tail_write_count <= elem_count);
        memcpy(&rb->memory[tail_write_index * rb->elem_size], data_in, tail_write_count * rb->elem_size);
        // then, if necessary, the head write
        size_t head_write_count = elem_count - tail_write_count;
        if (head_write_count > 0) {
            memcpy(&rb->memory[0], data_in + tail_write_count * rb->elem_size, head_write_count * rb->elem_size);
        }
        rb->write_idx += elem_count;
        if (rb->blocked_read) {
            wakeup_reader = true;
        }
    }
    assert(ringbuf_space_locked(rb) + elem_count == space);
    mutex_unlock(&rb->mutex);
    if (wakeup_reader) {
        semaphore_give(&rb->unblock_read);
    }
    return elem_count;
}

size_t ringbuf_read(ringbuf_t *rb, void *data_out, size_t elem_count, ringbuf_flags_t flags) {
    assert(rb != NULL && data_out != NULL);
    mutex_lock(&rb->mutex);
    // first, if we're being asked to read more data than we have, limit it.
    size_t size = ringbuf_size_locked(rb);
    if ((flags & RB_BLOCKING) && size == 0) {
        assert(!rb->blocked_read);
        rb->blocked_read = true;

        while (size == 0 && !rb->shutdown) {
            mutex_unlock(&rb->mutex);
            semaphore_take(&rb->unblock_read);
            mutex_lock(&rb->mutex);
            size = ringbuf_size_locked(rb);
        }

        assert(rb->blocked_read == true);
        rb->blocked_read = false;
    }
    if (elem_count > size) {
        elem_count = size;
    }
    bool wakeup_writer = false;
    if (elem_count > 0) {
        // might need up to two reads: a tail read, and a head read.
        size_t tail_read_index = mask(rb, rb->read_idx);
        // first, the tail read
        size_t tail_read_count = elem_count;
        if (tail_read_index + tail_read_count > rb->capacity) {
            tail_read_count = rb->capacity - tail_read_index;
        }
        assert(tail_read_count <= elem_count);
        memcpy(data_out, &rb->memory[tail_read_index * rb->elem_size], tail_read_count * rb->elem_size);
        // then, if necessary, the head read
        size_t head_read_count = elem_count - tail_read_count;
        if (head_read_count > 0) {
            memcpy(data_out + tail_read_count * rb->elem_size, &rb->memory[0], head_read_count * rb->elem_size);
        }
        rb->read_idx += elem_count;
        if (rb->blocked_write) {
            wakeup_writer = true;
        }
    }
    assert(ringbuf_size_locked(rb) + elem_count == size);
    mutex_unlock(&rb->mutex);
    if (wakeup_writer) {
        semaphore_give(&rb->unblock_write);
    }
    return elem_count;
}

size_t ringbuf_size(ringbuf_t *rb) {
    assert(rb != NULL);
    mutex_lock(&rb->mutex);
    size_t size = ringbuf_size_locked(rb);
    mutex_unlock(&rb->mutex);
    return size;
}

size_t ringbuf_space(ringbuf_t *rb) {
    assert(rb != NULL);
    mutex_lock(&rb->mutex);
    size_t space = ringbuf_space_locked(rb);
    mutex_unlock(&rb->mutex);
    return space;
}

int ringbuf_write_all(ringbuf_t *rb, void *data_in, size_t elem_count) {
    assert(rb != NULL);
    while (elem_count > 0) {
        size_t sent = ringbuf_write(rb, data_in, elem_count, RB_BLOCKING);
        if (sent == 0) {
            assert(rb->shutdown);
            return -1;
        }
        assert(sent > 0 && sent <= elem_count);
        elem_count -= sent;
        data_in += sent * rb->elem_size;
    }
    return 0;
}
