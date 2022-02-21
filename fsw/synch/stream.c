#include <string.h>

#include <hal/atomic.h>
#include <hal/debug.h>
#include <synch/stream.h>

void stream_set_writer(stream_t *stream, thread_t writer) {
    assert(stream != NULL && writer != NULL);
    if (stream->writer != NULL) {
        abortf("Stream already has writer %p registered, but writer %p also provided.", stream->writer, writer);
    }
    stream->writer = writer;
}

void stream_set_reader(stream_t *stream, thread_t reader) {
    assert(stream != NULL && reader != NULL);
    if (stream->reader != NULL) {
        abortf("Stream already has reader %p registered, but reader %p also provided.", stream->reader, reader);
    }
    stream->reader = reader;
}

// masks an unwrapped index into a valid array offset
static inline size_t mask(stream_t *stream, size_t index) {
    assert(stream != NULL);
    return index & (stream->capacity - 1);
}

static inline size_t stream_fill(stream_t *stream) {
    assert(stream != NULL);
    // only one of these atomicities will actually matter; we'll always be in a thread that owns one or the other.
    size_t fill = atomic_load(stream->write_idx) - atomic_load(stream->read_idx);
    assert(fill <= stream->capacity);
    return fill;
}

static inline size_t stream_space(stream_t *stream) {
    assert(stream != NULL);
    return stream->capacity - stream_fill(stream);
}

// blocks until there's at least one filled byte, and then returns the number of filled bytes
static inline size_t stream_take_fill(stream_t *stream) {
    assert(stream != NULL);
    size_t fill;
    while ((fill = stream_fill(stream)) == 0) {
        local_doze(stream->reader);
    }
    return fill;
}

// blocks until there's at least one free byte, and then returns the number of free bytes
static inline size_t stream_take_space(stream_t *stream) {
    assert(stream != NULL);
    size_t space;
    while ((space = stream_space(stream)) == 0) {
        local_doze(stream->writer);
    }
    return space;
}

static inline size_t stream_read_possible(stream_t *stream, uint8_t *data_out, size_t length, bool block) {
    assert(stream != NULL && data_out != NULL);
    // if we're being asked to read more data than we have, limit it.
    size_t fill = block ? stream_take_fill(stream) : stream_fill(stream);
    if (fill == 0) {
        return 0;
    }
    if (length > fill) {
        length = fill;
    }
    // if we're being asked to read more data than remains in the tail portion of the buffer, limit it
    // (we'll loop around another time to read the initial head portion of the buffer too)
    size_t read_index = mask(stream, stream->read_idx);
    if (read_index + length > stream->capacity) {
        length = stream->capacity - read_index;
    }
    // perform raw memory read
    memcpy(data_out, &stream->memory[read_index], length);
    // advance read index
    atomic_store(stream->read_idx, stream->read_idx + length);
    // return how much we actually wrote
    assert(length >= 1);
    return length;
}

// may only be used by a single thread at a time
size_t stream_read(stream_t *stream, uint8_t *data_out, size_t max_length, bool block) {
    assert(stream != NULL && data_out != NULL && stream->reader != NULL && stream->writer != NULL);
    // read at least one byte, blocking if necessary and requested
    size_t total_read = stream_read_possible(stream, data_out, max_length, block);
    if (total_read == 0 && !block) {
        return 0;
    }
    assert(total_read >= 1 && total_read <= max_length);
    // if we could read more, then do non-blocking reads until we've gotten all of it,
    // and return once we stop making any progress.
    while (total_read < max_length) {
        size_t additional = stream_read_possible(stream, data_out + total_read, max_length - total_read, false);
        if (additional == 0) {
            // done making progress; we cannot go any further without blocking, so return now.
            break;
        }
        total_read += additional;
        assert(total_read <= max_length);
    }
    // wake up the other end, now that more free space is available to it
    local_rouse(stream->writer);
    return total_read;
}

static inline size_t stream_write_possible(stream_t *stream, uint8_t *data_in, size_t length, bool block) {
    assert(stream != NULL && data_in != NULL);
    // if we're being asked to write more data than there is free space, limit it.
    size_t space = block ? stream_take_space(stream) : stream_space(stream);
    if (space == 0 && !block) {
        return 0;
    }
    if (length > space) {
        length = space;
    }
    // if we're being asked to write more data than fits in the remaining tail portion of the buffer, limit it
    // (we'll loop around another time to fill the initial head portion of the buffer too)
    size_t write_index = mask(stream, stream->write_idx);
    if (write_index + length > stream->capacity) {
        length = stream->capacity - write_index;
    }
    // perform raw memory write
    memcpy(&stream->memory[write_index], data_in, length);
    // advance write index
    atomic_store(stream->write_idx, stream->write_idx + length);
    // wake up the other end
    local_rouse(stream->reader);
    // return how much we actually wrote
    return length;
}

// may only be used by a single thread at a time
void stream_write(stream_t *stream, uint8_t *data_in, size_t length) {
    assert(stream != NULL && data_in != NULL && stream->reader != NULL && stream->writer != NULL);
    while (length > 0) {
        // write as much as we can (at least one byte)
        size_t actual = stream_write_possible(stream, data_in, length, true);
        assert(actual >= 1 && actual <= length);
        // try again with the remaining portion
        length  -= actual;
        data_in += actual;
    }
}

size_t stream_write_nonblock(stream_t *stream, uint8_t *data_in, size_t length) {
    assert(stream != NULL && data_in != NULL && stream->reader != NULL && stream->writer != NULL);
    return stream_write_possible(stream, data_in, length, false);
}
