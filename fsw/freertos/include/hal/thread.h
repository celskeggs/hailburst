#ifndef FSW_FREERTOS_HAL_THREAD_H
#define FSW_FREERTOS_HAL_THREAD_H

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <FreeRTOS.h>
#include "semphr.h"

typedef struct {
    TaskHandle_t      handle;
    SemaphoreHandle_t done;
    void           *(*start_routine)(void*);
    void             *arg;
} *thread_t;
typedef SemaphoreHandle_t mutex_t;

struct cond_local_wait {
    TaskHandle_t thread;
    struct cond_local_wait *next;
};

typedef struct {
    mutex_t state_mutex;
    struct cond_local_wait *queue;
} cond_t;

extern void thread_create(thread_t *out, const char *name, unsigned int priority, void *(*start_routine)(void*), void *arg);
extern void thread_join(thread_t thread);
extern void thread_cancel(thread_t thread);
extern void thread_time_now(struct timespec *tp);
extern bool thread_join_timed(thread_t thread, const struct timespec *abstime); // true on success, false on timeout
extern void thread_disable_cancellation(void);
extern void thread_enable_cancellation(void);
extern void thread_testcancel(void);

extern void mutex_init(mutex_t *mutex);
extern void mutex_destroy(mutex_t *mutex);

static inline void mutex_lock(mutex_t *mutex) {
    BaseType_t status;
    assert(mutex != NULL && *mutex != NULL);
    status = xSemaphoreTake(*mutex, portMAX_DELAY);
    assert(status == pdTRUE); // should always be obtained, because we have support for vTaskSuspend
}

static inline void mutex_unlock(mutex_t *mutex) {
    BaseType_t status;
    assert(mutex != NULL && *mutex != NULL);
    status = xSemaphoreGive(*mutex);
    assert(status == pdTRUE); // should always be released, because we should have acquired it earlier
}

extern void cond_init(cond_t *cond);
extern void cond_destroy(cond_t *cond);

static inline void cond_broadcast(cond_t *cond) {
    mutex_lock(&cond->state_mutex);
    for (struct cond_local_wait *entry = cond->queue; entry != NULL; entry = entry->next) {
        assert(entry->thread != NULL);
        xTaskNotifyGive(entry->thread);
    }
    cond->queue = NULL;
    mutex_unlock(&cond->state_mutex);
}

static inline void cond_wait_freertos_ticks(cond_t *cond, mutex_t *mutex, TickType_t ticks) {
    mutex_lock(&cond->state_mutex);

    // insert ourselves into the wait queue
    struct cond_local_wait wait_entry = {
        .thread = xTaskGetCurrentTaskHandle(),
        .next   = cond->queue,
    };
    cond->queue = &wait_entry;

    mutex_unlock(&cond->state_mutex);
    mutex_unlock(mutex);

    if (ulTaskNotifyTake(pdTRUE, ticks) == 0) {
        // not woken up; remove ourselves from the wait queue
        mutex_lock(&cond->state_mutex);

        bool ok = false;
        for (struct cond_local_wait **entry_ptr = &cond->queue; *entry_ptr != NULL; entry_ptr = &(*entry_ptr)->next) {
            if (*entry_ptr == &wait_entry) {
                *entry_ptr = wait_entry.next;
                wait_entry.thread = NULL;
                wait_entry.next = NULL;
                ok = true;
                break;
            }
        }

        mutex_unlock(&cond->state_mutex);

        // if we couldn't find ourselves, that means we must have been notified after our wakeup.
        if (!ok) {
            // make sure to clear the task notification!
            // TODO: is this actually necessary?
            BaseType_t cleared = xTaskNotifyStateClear(NULL);
            assert(cleared == pdTRUE);
        }
    }

    mutex_lock(mutex);
}

static inline void cond_wait(cond_t *cond, mutex_t *mutex) {
    cond_wait_freertos_ticks(cond, mutex, portMAX_DELAY);
}

static inline void cond_timedwait(cond_t *cond, mutex_t *mutex, uint64_t nanoseconds) {
    TickType_t ticks = pdMS_TO_TICKS(nanoseconds / 1000000);
    if (ticks == 0 && nanoseconds > 0) {
        ticks = 1;
    }
    cond_wait_freertos_ticks(cond, mutex, ticks);
}

#endif /* FSW_FREERTOS_HAL_THREAD_H */
