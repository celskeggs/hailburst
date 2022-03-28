#ifndef FSW_SYNCH_PIPEBUF_H
#define FSW_SYNCH_PIPEBUF_H

/*
 * This file contains buffering support for both the send and receive ends of pipes. This allows output to be built up
 * incrementally and only transmitted when possible, and allows input to be consumed incrementally and only received as
 * necessary.
 *
 * The capacity of these buffers must be at least the transfer rate of the underlying pipes. However, it is recommended
 * that the buffers are sized at twice the transfer rate of the underlying pipes to avoid edge cases that slow down
 * transfers.
 */

#include <string.h>

#include <synch/pipe.h>

typedef struct {
    // immutable
    pipe_t * const  pipe;
    const uint8_t   replica_id;
    const size_t    scratch_capacity;
    uint8_t * const scratch;
    // mutable
    size_t scratch_offset;
} pipe_sender_t;

typedef struct {
    // immutable
    pipe_t * const  pipe;
    const uint8_t   replica_id;
    const size_t    scratch_capacity;
    uint8_t * const scratch;
    // mutable
    size_t scratch_avail;
    size_t scratch_offset;
} pipe_receiver_t;

macro_define(PIPE_SENDER_REGISTER, s_ident, s_pipe, s_capacity, s_replica) {
    uint8_t symbol_join(s_ident, scratch_buffer)[s_capacity];
    pipe_sender_t s_ident = {
        .pipe = &(s_pipe),
        .replica_id = (s_replica),
        .scratch_capacity = (s_capacity),
        .scratch_offset = 0,
        .scratch = symbol_join(s_ident, scratch_buffer),
    }
}

macro_define(PIPE_RECEIVER_REGISTER, r_ident, r_pipe, r_capacity, r_replica) {
    uint8_t symbol_join(r_ident, scratch_buffer)[r_capacity];
    pipe_receiver_t r_ident = {
        .pipe = &(r_pipe),
        .replica_id = (r_replica),
        .scratch_capacity = (r_capacity),
        .scratch_avail = 0,
        .scratch_offset = 0,
        .scratch = symbol_join(r_ident, scratch_buffer),
    }
}

void pipe_sender_reset(pipe_sender_t *s);
void pipe_sender_prepare(pipe_sender_t *s);
void pipe_sender_commit(pipe_sender_t *s);

static inline bool pipe_sender_reserve(pipe_sender_t *s, size_t length) {
    assert(s != NULL);
    assert(length <= s->scratch_capacity);
    assert(s->scratch_offset <= s->scratch_capacity);
    return s->scratch_offset + length <= s->scratch_capacity;
}

static inline void pipe_sender_write_byte(pipe_sender_t *s, uint8_t byte) {
    assert(s->scratch_offset < s->scratch_capacity);
    s->scratch[s->scratch_offset++] = byte;
}

static inline void pipe_sender_write(pipe_sender_t *s, void *data, size_t length) {
    assert(s->scratch_offset + length <= s->scratch_capacity);
    memcpy(&s->scratch[s->scratch_offset], data, length);
    s->scratch_offset += length;
}

static inline size_t pipe_sender_write_partial(pipe_sender_t *s, void *data, size_t length) {
    assert(s->scratch_offset <= s->scratch_capacity);
    if (s->scratch_offset + length > s->scratch_capacity) {
        length = s->scratch_capacity - s->scratch_offset;
    }
    if (length > 0) {
        memcpy(&s->scratch[s->scratch_offset], data, length);
    }
    return length;
}

void pipe_receiver_reset(pipe_receiver_t *r);
void pipe_receiver_prepare(pipe_receiver_t *r);
void pipe_receiver_commit(pipe_receiver_t *r);

static inline bool pipe_receiver_has_next(pipe_receiver_t *r, size_t count) {
    assert(r != NULL);
    assert(r->scratch_avail <= r->scratch_capacity);
    assert(r->scratch_offset <= r->scratch_avail);
    return r->scratch_offset + count <= r->scratch_avail;
}

static inline uint8_t pipe_receiver_read_byte(pipe_receiver_t *r) {
    assert(r->scratch_offset < r->scratch_avail);
    return r->scratch[r->scratch_offset++];
}

static inline uint8_t pipe_receiver_peek_byte(pipe_receiver_t *r) {
    assert(r->scratch_offset < r->scratch_avail);
    return r->scratch[r->scratch_offset];
}

#endif /* FSW_SYNCH_PIPEBUF_H */
