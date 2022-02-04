#ifndef FSW_FREERTOS_HAL_EPLOCK_H
#define FSW_FREERTOS_HAL_EPLOCK_H

/*
 * This file provides the Linux implementation of an Epoch Lock. This is a mock implementation of something that is
 * much simpler on FreeRTOS.
 */

#include <hal/atomic.h>
#include <hal/clock.h>
#include <hal/thread.h>

typedef struct {
    mutex_t        mutex;
    pthread_cond_t cond;

    thread_t holder;
    uint32_t hold_marker;
} eplock_t;

#define EPLOCK_REGISTER(e_ident)                                                                                      \
    eplock_t e_ident = { .holder = NULL, .hold_marker = 0 };                                                          \
    static void e_ident ## _init(void) {                                                                              \
        mutex_init(&e_ident.mutex);                                                                                   \
        pthread_condattr_t attr;                                                                                      \
        THREAD_CHECK(pthread_condattr_init(&attr));                                                                   \
        THREAD_CHECK(pthread_condattr_setclock(&attr, CLOCK_MONOTONIC));                                              \
        THREAD_CHECK(pthread_cond_init(&e_ident.cond, &attr));                                                        \
        THREAD_CHECK(pthread_condattr_destroy(&attr));                                                                \
    }                                                                                                                 \
    PROGRAM_INIT(STAGE_RAW, e_ident ## _init);

static inline void eplock_acquire(eplock_t *lock) {
    assert(lock != NULL);
    mutex_lock(&lock->mutex);
    assert(lock->holder == NULL);
    atomic_store_relaxed(lock->holder, task_get_current());
    assert(lock->holder != NULL);
}

// On Linux, releases the held eplock, waits until another thread acquires and then releases the eplock, and then
// acquires the eplock. Other calls to eplock_wait_ready are not counted as acquires and releases for the purposes of
// this function.
// If this condition is not satisfied within two milliseconds, then false is returned.
// On FreeRTOS, always returns false immediately.
// (The duct implementation calls this function when it's waiting on a peer that is not yet done running, and asserts
//  if false is returned.)
static inline bool eplock_wait_ready(eplock_t *lock, uint64_t deadline_ns) {
    assert(lock != NULL);
    assert(lock->holder == task_get_current());
    atomic_store_relaxed(lock->holder, NULL);

    uint32_t base_hold_marker = lock->hold_marker;

    // this is possible because clock_timestamp_monotonic() uses CLOCK_MONOTONIC,
    // and we set our condition variable to CLOCK_MONOTONIC as well above.
    struct timespec deadline_ts = {
        .tv_sec  = deadline_ns / CLOCK_NS_PER_SEC,
        .tv_nsec = deadline_ns % CLOCK_NS_PER_SEC,
    };

    while (lock->hold_marker == base_hold_marker) {
        int retcode = pthread_cond_timedwait(&lock->cond, &lock->mutex, &deadline_ts);
        if (retcode == ETIMEDOUT) {
            assert(clock_timestamp_monotonic() >= deadline_ns);
            break;
        } else if (retcode != 0 && retcode != EINTR) {
            fprintf(stderr, "thread error: %d in eplock_wait_ready\n", retcode);
            abort();
        }
    }

    assert(lock->holder == NULL);
    atomic_store_relaxed(lock->holder, task_get_current());
    assert(lock->holder != NULL);
    return lock->hold_marker != base_hold_marker;
}

static inline void eplock_release(eplock_t *lock) {
    assert(lock != NULL);
    assert(lock->holder == task_get_current());
    atomic_store_relaxed(lock->holder, NULL);
    lock->hold_marker += 1;
    mutex_unlock(&lock->mutex);
}

static inline bool eplock_held(eplock_t *lock) {
    assert(lock != NULL);
    // no race condition: in the case of a simultaneous mutation, both the old and new values of 'holder' will be
    // different from task_get_current, so the result remains the same.
    return atomic_load_relaxed(lock->holder) == task_get_current();
}

static inline void eplock_wait_next_epoch(void) {
    /* approximate implementation */
    usleep(1000);
}

#endif /* FSW_FREERTOS_HAL_EPLOCK_H */
