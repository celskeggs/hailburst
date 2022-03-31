#ifndef FSW_SYNCH_CIRCULAR_H
#define FSW_SYNCH_CIRCULAR_H

/*
 * This file contains an implementation of a single-threaded circular buffer data structure.
 */

#include <stdint.h>

#include <hal/debug.h>

typedef uint32_t circ_index_t;

typedef const struct {
    size_t       element_size;
    circ_index_t element_count;
    uint8_t     *element_storage;
    struct circ_buf_mut {
        // these both wrap at 2 * element_count
        circ_index_t next_read;
        circ_index_t next_write;
    } *mut;
} circ_buf_t;

macro_define(CIRC_BUF_REGISTER, c_ident, c_element_size, c_element_count) {
    static_assert(c_element_size > 0 && c_element_size == (size_t) c_element_size, "positive note size");
    static_assert(c_element_count > 0 && c_element_count == (circ_index_t) c_element_count, "positive note count");
    uint8_t symbol_join(c_ident, storage)[(c_element_size) * (c_element_count)];
    struct circ_buf_mut symbol_join(c_ident, mutable) = {
        .next_read = 0,
        .next_write = 0,
    };
    circ_buf_t c_ident = {
        .element_size = (c_element_size),
        .element_count = (c_element_count),
        .element_storage = symbol_join(c_ident, storage),
        .mut = &symbol_join(c_ident, mutable),
    }
}

static inline size_t circ_buf_elem_size(circ_buf_t *c) {
    assert(c != NULL);
    return c->element_size;
}

static inline circ_index_t circ_buf_elem_count(circ_buf_t *c) {
    assert(c != NULL);
    return c->element_count;
}

static inline void *circ_buf_get_element(circ_buf_t *c, circ_index_t index) {
    assert(c != NULL && index < c->element_count && c->element_storage != NULL);
    return &c->element_storage[c->element_size * index];
}

// function to call on clip/task restart, to ensure that the circular buffer is in a safe state.
static inline void circ_buf_reset(circ_buf_t *c) {
    assert(c != NULL && c->mut != NULL);
    c->mut->next_read = 0;
    c->mut->next_write = 0;
}

// return the number of elements available to be read
static inline circ_index_t circ_buf_read_avail(circ_buf_t *c) {
    assert(c != NULL && c->mut != NULL);
    // write leads, read lags
    circ_index_t ahead = (c->mut->next_write - c->mut->next_read + 2 * c->element_count) % (2 * c->element_count);
    assertf(ahead <= c->element_count, "ahead=%u, element_count=%u", ahead, c->element_count);
    return ahead;
}

// return a pointer to the data in one of the next readable elements, or NULL if no next readable element.
static inline void *circ_buf_read_peek(circ_buf_t *c, circ_index_t index) {
    assert(c != NULL && c->mut != NULL);
    if (index < circ_buf_read_avail(c)) {
        return circ_buf_get_element(c, (c->mut->next_read + index) % c->element_count);
    } else {
        return NULL;
    }
}

// once the data seen in peek has been consumed, call this to advance the read pointer.
static inline void circ_buf_read_done(circ_buf_t *c, circ_index_t count) {
    assert(c != NULL);
    assert(1 <= count && count <= circ_buf_read_avail(c));
    c->mut->next_read += count;
}

// return the number of elements available to be written
static inline circ_index_t circ_buf_write_avail(circ_buf_t *c) {
    assert(c != NULL);
    return c->element_count - circ_buf_read_avail(c);
}

// return a pointer to the data in one of the next writable elements, or NULL if no next writable element.
static inline void *circ_buf_write_peek(circ_buf_t *c, circ_index_t index) {
    assert(c != NULL && c->mut != NULL);
    if (index < circ_buf_write_avail(c)) {
        return circ_buf_get_element(c, (c->mut->next_write + index) % c->element_count);
    } else {
        return NULL;
    }
}

// once data has been written to the buffer provided by peek, call this to advance the read pointer.
static inline void circ_buf_write_done(circ_buf_t *c, circ_index_t count) {
    assert(c != NULL);
    assert(1 <= count && count <= circ_buf_write_avail(c));
    c->mut->next_write += count;
}

#endif /* FSW_SYNCH_CIRCULAR_H */
