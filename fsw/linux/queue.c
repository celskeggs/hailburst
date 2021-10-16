#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <fsw/debug.h>
#include <hal/thread.h>

void queue_init(queue_t *queue, size_t item_size, size_t capacity) {
    assert(queue != NULL);
    assert(item_size >= 1 && capacity >= 1);
    // make sure 'completely empty' and 'completely full' can be distinguished
    assert(((capacity << 1) >> 1) == capacity);

    mutex_init(&queue->mutex);
    pthread_condattr_t attr;
    THREAD_CHECK(pthread_condattr_init(&attr));
    THREAD_CHECK(pthread_condattr_setclock(&attr, CLOCK_MONOTONIC));
    THREAD_CHECK(pthread_cond_init(&queue->cond, &attr));
    THREAD_CHECK(pthread_condattr_destroy(&attr));

    queue->memory = malloc(item_size * capacity);
    assert(queue->memory != NULL);
    queue->capacity = capacity;
    queue->item_size = item_size;
    queue->read_scroll = queue->write_scroll = 0;
}

void queue_destroy(queue_t *queue) {
    assert(queue != NULL && queue->memory != NULL);

    free(queue->memory);
    THREAD_CHECK(pthread_cond_destroy(&queue->cond));
    mutex_destroy(&queue->mutex);

    // wipe memory to make use-after-destroy bugs more obvious
    memset(queue, 0, sizeof(queue_t));
}

// 'scroll': scroll point: an index, except that it goes up to twice the buffer size and wraps around more slowly.
// this allows for different meanings of "scroll points equal" and "scroll points equal modulo capacity"
static inline size_t queue_scroll_to_index(queue_t *queue, size_t scroll) {
    assert(scroll < 2 * queue->capacity);
    return scroll % queue->capacity;
}

static inline void *queue_scroll_to_elemptr(queue_t *queue, size_t scroll) {
    return &queue->memory[queue_scroll_to_index(queue, scroll) * queue->item_size];
}

static inline size_t queue_scroll_plus_one(queue_t *queue, size_t scroll) {
    assert(scroll < 2 * queue->capacity);
    return (scroll + 1) % (queue->capacity * 2);
}

// _locked means the function assumes the lock is held
static inline size_t queue_readable_items_locked(queue_t *queue) {
    size_t size = (queue->write_scroll - queue->read_scroll) % (2 * queue->capacity);
    assert(size <= queue->capacity);
    return size;
}

static inline size_t queue_writable_spaces_locked(queue_t *queue) {
    return queue->capacity - queue_readable_items_locked(queue);
}

void queue_send(queue_t *queue, const void *new_item) {
    assert(queue != NULL && new_item != NULL);
    mutex_lock(&queue->mutex);
    // wait until we have room to send
    while (queue_writable_spaces_locked(queue) == 0) {
        THREAD_CHECK(pthread_cond_wait(&queue->cond, &queue->mutex));
    }
    // perform send
    memcpy(queue_scroll_to_elemptr(queue, queue->write_scroll), new_item, queue->item_size);
    queue->write_scroll = queue_scroll_plus_one(queue, queue->write_scroll);
    // notify other waiters
    THREAD_CHECK(pthread_cond_broadcast(&queue->cond));
    mutex_unlock(&queue->mutex);
}

// returns true if sent, false if not
bool queue_send_try(queue_t *queue, void *new_item) {
    assert(queue != NULL && new_item != NULL);
    bool sent = false;
    mutex_lock(&queue->mutex);
    // do we have room to write?
    if (queue_writable_spaces_locked(queue) > 0) {
        // perform send
        memcpy(queue_scroll_to_elemptr(queue, queue->write_scroll), new_item, queue->item_size);
        queue->write_scroll = queue_scroll_plus_one(queue, queue->write_scroll);
        // notify waiters
        THREAD_CHECK(pthread_cond_broadcast(&queue->cond));
        // notify caller
        sent = true;
    }
    mutex_unlock(&queue->mutex);
    return sent;
}

void queue_recv(queue_t *queue, void *new_item) {
    assert(queue != NULL && new_item != NULL);
    mutex_lock(&queue->mutex);
    // wait until we have an item to receive
    while (queue_readable_items_locked(queue) == 0) {
        THREAD_CHECK(pthread_cond_wait(&queue->cond, &queue->mutex));
    }
    // perform receive
    memcpy(new_item, queue_scroll_to_elemptr(queue, queue->read_scroll), queue->item_size);
    queue->read_scroll = queue_scroll_plus_one(queue, queue->read_scroll);
    // notify waiters
    THREAD_CHECK(pthread_cond_broadcast(&queue->cond));
    mutex_unlock(&queue->mutex);
}

bool queue_recv_try(queue_t *queue, void *new_item) {
    assert(queue != NULL && new_item != NULL);
    bool received = false;
    mutex_lock(&queue->mutex);
    // wait until we have an item to receive
    if (queue_readable_items_locked(queue) > 0) {
        // perform receive
        memcpy(new_item, queue_scroll_to_elemptr(queue, queue->read_scroll), queue->item_size);
        queue->read_scroll = queue_scroll_plus_one(queue, queue->read_scroll);
        // notify waiters
        THREAD_CHECK(pthread_cond_broadcast(&queue->cond));
        // notify caller
        received = true;
    }
    mutex_unlock(&queue->mutex);
    return received;
}

// returns true if received, false if timed out
bool queue_recv_timed_abs(queue_t *queue, void *new_item, uint64_t deadline_ns) {
    assert(queue != NULL && new_item != NULL);
    struct timespec deadline_ts;

    // this is possible because clock_timestamp_monotonic() uses CLOCK_MONOTONIC_RAW,
    // and we set our condition variable to CLOCK_MONOTONIC_RAW as well above.
    deadline_ts.tv_sec  = deadline_ns / NS_PER_SEC;
    deadline_ts.tv_nsec = deadline_ns % NS_PER_SEC;

    mutex_lock(&queue->mutex);
    // wait until we have an item to receive
    while (queue_readable_items_locked(queue) == 0) {
        if (!THREAD_CHECK_OK(pthread_cond_timedwait(&queue->cond, &queue->mutex, &deadline_ts), ETIMEDOUT)) {
            mutex_unlock(&queue->mutex);
            return false;
        }
    }
    // perform receive
    memcpy(new_item, queue_scroll_to_elemptr(queue, queue->read_scroll), queue->item_size);
    queue->read_scroll = queue_scroll_plus_one(queue, queue->read_scroll);
    // notify waiters
    THREAD_CHECK(pthread_cond_broadcast(&queue->cond));
    mutex_unlock(&queue->mutex);
    return true;
}
