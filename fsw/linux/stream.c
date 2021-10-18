#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <fsw/debug.h>
#include <hal/thread.h>

void stream_init(stream_t *stream, size_t capacity) {
    assert(stream != NULL);
    mutex_init(&stream->mutex);
    semaphore_init(&stream->unblock_write);
    semaphore_init(&stream->unblock_read);
    // make sure this is a power of two
    assert((capacity & (capacity - 1)) == 0);
    // make sure at least one bit is free
    assert((capacity << 1) != 0);

    stream->memory = malloc(capacity);
    assert(stream->memory != NULL);
    stream->capacity = capacity;
    stream->read_idx = stream->write_idx = 0;
}

void stream_destroy(stream_t *stream) {
    assert(stream != NULL && stream->memory != NULL);

    // free memory
    free(stream->memory);
    stream->memory = NULL;

    // tear down pthread objects
    semaphore_destroy(&stream->unblock_write);
    semaphore_destroy(&stream->unblock_read);
    mutex_destroy(&stream->mutex);

    // wipe memory to make use-after-destroy bugs more obvious
    memset(stream, 0, sizeof(stream_t));
}

// masks an unwrapped index into a valid array offset
static inline size_t mask(stream_t *stream, size_t index) {
    assert(stream != NULL);
    return index & (stream->capacity - 1);
}

// _locked means the function assumes the lock is held
static inline size_t stream_size_locked(stream_t *stream) {
    assert(stream != NULL);
    size_t size = stream->write_idx - stream->read_idx;
    assert(size <= stream->capacity);
    return size;
}

static inline size_t stream_space_locked(stream_t *stream) {
    assert(stream != NULL);
    return stream->capacity - stream_size_locked(stream);
}

// may only be used by a single thread at a time
void stream_write(stream_t *stream, uint8_t *data_in, size_t length) {
    assert(stream != NULL && data_in != NULL);
    while (length > 0) {
        size_t current_len = length;
        mutex_lock(&stream->mutex);
        // first, if we're being asked to write more data than we can, limit it.
        size_t space = stream_space_locked(stream);
        if (space == 0) {
            assert(!stream->blocked_write);
            stream->blocked_write = true;

            while (space == 0) {
                mutex_unlock(&stream->mutex);
                semaphore_take(&stream->unblock_write);
                mutex_lock(&stream->mutex);
                space = stream_space_locked(stream);
            }

            assert(stream->blocked_write == true);
            stream->blocked_write = false;
        }
        if (current_len > space) {
            current_len = space;
        }
        bool wakeup_reader = false;
        if (current_len > 0) {
            // might need up to two writes: a tail write, and a head write.
            size_t tail_write_index = mask(stream, stream->write_idx);
            // first, the tail write
            size_t tail_write_count = current_len;
            if (tail_write_index + tail_write_count > stream->capacity) {
                tail_write_count = stream->capacity - tail_write_index;
            }
            assert(tail_write_count <= current_len);
            memcpy(&stream->memory[tail_write_index], data_in, tail_write_count);
            // then, if necessary, the head write
            size_t head_write_count = current_len - tail_write_count;
            if (head_write_count > 0) {
                memcpy(&stream->memory[0], data_in + tail_write_count, head_write_count);
            }
            stream->write_idx += current_len;
            if (stream->blocked_read) {
                wakeup_reader = true;
            }
        }
        assert(stream_space_locked(stream) + current_len == space);
        mutex_unlock(&stream->mutex);
        if (wakeup_reader) {
            semaphore_give(&stream->unblock_read);
        }
        assert(current_len <= length);
        length -= current_len;
        data_in += current_len;
    }
}

size_t stream_read(stream_t *stream, uint8_t *data_out, size_t length) {
    assert(stream != NULL && data_out != NULL);
    mutex_lock(&stream->mutex);
    // first, if we're being asked to read more data than we have, limit it.
    size_t size = stream_size_locked(stream);
    if (size == 0) {
        assert(!stream->blocked_read);
        stream->blocked_read = true;

        while (size == 0) {
            mutex_unlock(&stream->mutex);
            semaphore_take(&stream->unblock_read);
            mutex_lock(&stream->mutex);
            size = stream_size_locked(stream);
        }

        assert(stream->blocked_read == true);
        stream->blocked_read = false;
    }
    if (length > size) {
        length = size;
    }
    bool wakeup_writer = false;
    if (length > 0) {
        // might need up to two reads: a tail read, and a head read.
        size_t tail_read_index = mask(stream, stream->read_idx);
        // first, the tail read
        size_t tail_read_count = length;
        if (tail_read_index + tail_read_count > stream->capacity) {
            tail_read_count = stream->capacity - tail_read_index;
        }
        assert(tail_read_count <= length);
        memcpy(data_out, &stream->memory[tail_read_index], tail_read_count);
        // then, if necessary, the head read
        size_t head_read_count = length - tail_read_count;
        if (head_read_count > 0) {
            memcpy(data_out + tail_read_count, &stream->memory[0], head_read_count);
        }
        stream->read_idx += length;
        if (stream->blocked_write) {
            wakeup_writer = true;
        }
    }
    assert(stream_size_locked(stream) + length == size);
    mutex_unlock(&stream->mutex);
    if (wakeup_writer) {
        semaphore_give(&stream->unblock_write);
    }
    return length;
}
