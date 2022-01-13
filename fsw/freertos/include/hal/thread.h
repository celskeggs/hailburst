#ifndef FSW_FREERTOS_HAL_THREAD_H
#define FSW_FREERTOS_HAL_THREAD_H

#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include <FreeRTOS.h>
#include <semphr.h>

#include <rtos/timer.h>
#include <fsw/debug.h>

typedef TCB_t *thread_t;
typedef SemaphoreHandle_t semaphore_t;

// TODO: make the entrypoint parameter strongly typed like PROGRAM_INIT_PARAM
#define TASK_REGISTER(t_ident, t_name, t_priority, t_start, t_arg, t_restartable) \
    static_assert(t_priority < configMAX_PRIORITIES, "invalid priority");         \
    StackType_t t_ident ## _stack[RTOS_STACK_SIZE];                               \
    __attribute__((section(".tasktable"))) TCB_t t_ident = {                      \
        .pxTopOfStack    = NULL,              \
        .start_routine   = t_start,           \
        .start_arg       = t_arg,             \
        .restartable     = t_restartable,     \
        .needs_restart   = false,             \
        .hit_restart     = false,             \
        /* no init for lists here */          \
        .uxPriority      = t_priority,        \
        .pxStack         = t_ident ## _stack, \
        .pcTaskName      = t_name,            \
        .ulNotifiedValue = { 0 },             \
        .ucNotifyState   = { 0 },             \
    }

// TODO: fix write to uxPriority

static inline thread_t task_get_current(void) {
    TaskHandle_t handle = xTaskGetCurrentTaskHandle();
    assert(handle != NULL);
    return handle;
}

void task_suspend(void) __attribute__((noreturn));

// TODO: more efficient semaphore preallocation approach
#define SEMAPHORE_REGISTER(s_ident) \
    semaphore_t s_ident; \
    PROGRAM_INIT_PARAM(STAGE_READY, semaphore_init, s_ident, &s_ident);

// semaphores are created empty, such that an initial take will block
extern void semaphore_init(semaphore_t *sema);
extern void semaphore_destroy(semaphore_t *sema);

static inline void semaphore_take(semaphore_t *sema) {
    BaseType_t status;
    assert(sema != NULL && *sema != NULL);
    status = xSemaphoreTake(*sema, portMAX_DELAY);
    assert(status == pdTRUE);
}

// returns true if taken, false if not available
static inline bool semaphore_take_try(semaphore_t *sema) {
    assert(sema != NULL && *sema != NULL);
    return xSemaphoreTake(*sema, 0) == pdTRUE;
}

// returns true if taken, false if timed out
static inline bool semaphore_take_timed(semaphore_t *sema, uint64_t nanoseconds) {
    assert(sema != NULL && *sema != NULL);
    return xSemaphoreTake(*sema, timer_ns_to_ticks(nanoseconds)) == pdTRUE;
}

// returns true if taken, false if timed out
static inline bool semaphore_take_timed_abs(semaphore_t *sema, uint64_t deadline_ns) {
    assert(sema != NULL && *sema != NULL);
    return xSemaphoreTake(*sema, timer_ticks_until_ns(deadline_ns)) == pdTRUE;
}

static inline bool semaphore_give(semaphore_t *sema) {
    assert(sema != NULL && *sema != NULL);
    return xSemaphoreGive(*sema) == pdTRUE;
}

#endif /* FSW_FREERTOS_HAL_THREAD_H */
