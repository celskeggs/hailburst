#ifndef FSW_FREERTOS_HAL_EPLOCK_H
#define FSW_FREERTOS_HAL_EPLOCK_H

/*
 * This file provides the FreeRTOS implementation of an Epoch Lock. This is a locking mechanism based on the partition
 * scheduler's inherent properties. (Which is emulated by a more ordinary lock on Linux.)
 *
 * Basically: each task must acquire and release the lock *within the same scheduling period*. Rather than actually
 * waiting for the last task to complete, the FreeRTOS implementation will simply *assert* if the last task has not
 * completed yet, which implies that it had overrun its deadline.
 */

#include <hal/atomic.h>
#include <hal/thread.h>

typedef struct {
    thread_t holder;
} eplock_t;

#define EPLOCK_INIT     ((eplock_t) { .holder = NULL })

static inline void eplock_acquire(eplock_t *lock) {
    assert(lock != NULL);
    thread_t current_task = task_get_current();
    assert(current_task != NULL);
    thread_t previous_task = atomic_exchange(lock->holder, current_task);
    assertf(previous_task == NULL, "eplock could not be acquired by task %s: task %s failed to meet its deadline",
            current_task->pcTaskName, previous_task->pcTaskName);
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
    (void) deadline_ns;
    return false;
}

static inline void eplock_release(eplock_t *lock) {
    assert(lock != NULL);
    thread_t current_task = task_get_current();
    assert(current_task != NULL);
    thread_t previous_task = atomic_exchange(lock->holder, NULL);
    assertf(previous_task == current_task, "eplock could not be released by task %s: task %s unexpectedly held lock",
            current_task->pcTaskName, previous_task->pcTaskName);
}

static inline bool eplock_held(eplock_t *lock) {
    assert(lock != NULL);
    thread_t current_task = task_get_current();
    assert(current_task != NULL);
    return current_task == atomic_load_relaxed(lock->holder);
}

static inline void eplock_wait_next_epoch(void) {
    taskYIELD();
}

#endif /* FSW_FREERTOS_HAL_EPLOCK_H */
