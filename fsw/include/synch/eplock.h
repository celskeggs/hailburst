#ifndef FSW_SYNCH_EPLOCK_H
#define FSW_SYNCH_EPLOCK_H

/*
 * This file provides an implementation of the Epoch Lock. This is a locking mechanism based on the partition
 * scheduler's inherent properties.
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

#define EPLOCK_REGISTER(e_ident)                                                                                      \
    eplock_t e_ident = { .holder = NULL }

static inline void eplock_acquire(eplock_t *lock) {
    assert(lock != NULL);
    thread_t current_task = task_get_current();
    assert(current_task != NULL);
    thread_t previous_task = atomic_exchange(lock->holder, current_task);
    assertf(previous_task == NULL, "eplock could not be acquired by task %s: task %s failed to meet its deadline",
            task_get_name(current_task), task_get_name(previous_task));
}

static inline void eplock_release(eplock_t *lock) {
    assert(lock != NULL);
    thread_t current_task = task_get_current();
    assert(current_task != NULL);
    thread_t previous_task = atomic_exchange(lock->holder, NULL);
    assertf(previous_task == current_task, "eplock could not be released by task %s: task %s unexpectedly held lock",
            task_get_name(current_task), task_get_name(previous_task));
}

static inline bool eplock_held(eplock_t *lock) {
    assert(lock != NULL);
    thread_t current_task = task_get_current();
    assert(current_task != NULL);
    return current_task == atomic_load_relaxed(lock->holder);
}

#endif /* FSW_SYNCH_EPLOCK_H */
