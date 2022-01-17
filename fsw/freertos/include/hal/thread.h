#ifndef FSW_FREERTOS_HAL_THREAD_H
#define FSW_FREERTOS_HAL_THREAD_H

#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include <FreeRTOS.h>
#include <task.h>

#include <rtos/timer.h>
#include <fsw/debug.h>
#include <fsw/preprocessor.h>

typedef TCB_t *thread_t;

#define TASK_PROTO(t_ident) \
    extern TCB_t t_ident;

#define TASK_REGISTER(t_ident, t_name, t_start, t_arg, t_restartable)             \
    StackType_t t_ident ## _stack[RTOS_STACK_SIZE];                               \
    TCB_mut_t t_ident ## _mutable = {                                             \
        .pxTopOfStack    = NULL,                                                  \
        .needs_restart   = false,                                                 \
        .hit_restart     = false,                                                 \
        /* no init for lists here */                                              \
        .ulNotifiedValue = { 0 },                                                 \
    };                                                                            \
    __attribute__((section(".tasktable"))) TCB_t t_ident = {                      \
        .mut             = &t_ident ## _mutable,                                  \
        .start_routine   = PP_ERASE_TYPE(t_start, t_arg),                         \
        .start_arg       = t_arg,                                                 \
        .restartable     = t_restartable,                                         \
        .pxStack         = t_ident ## _stack,                                     \
        .pcTaskName      = t_name,                                                \
    }

static inline thread_t task_get_current(void) {
    TaskHandle_t handle = xTaskGetCurrentTaskHandle();
    assert(handle != NULL);
    return handle;
}

void task_suspend(void) __attribute__((noreturn));

static inline void task_delay_abs(uint64_t deadline_ns) {
    while (timer_now_ns() < deadline_ns) {
        taskYIELD();
    }
}

static inline void task_delay(uint64_t nanoseconds) {
    task_delay_abs(timer_now_ns() + nanoseconds);
}

// top-level task doze/rouse: should only be used by the code that defines a task, not intermediate libraries

enum {
    NOTIFY_INDEX_TOP_LEVEL = 0,
    NOTIFY_INDEX_LOCAL     = 1,
};

static inline void task_rouse(thread_t task) {
    assert(task != NULL);
    BaseType_t result = xTaskNotifyGiveIndexed(task, NOTIFY_INDEX_TOP_LEVEL);
    assert(result == pdPASS);
}

static inline void task_rouse_from_isr(thread_t task) {
    assert(task != NULL);
    vTaskNotifyGiveIndexedFromISR(task, NOTIFY_INDEX_TOP_LEVEL);
}

// does not block
static inline bool task_doze_try(void) {
    return ulTaskNotifyTakeIndexed(NOTIFY_INDEX_TOP_LEVEL) > 0;
}

static inline void task_doze(void) {
    while (!task_doze_try()) {
        taskYIELD();
    }
}

static inline bool task_doze_timed_abs(uint64_t deadline_ns) {
    bool roused = task_doze_try();
    while (!roused && timer_now_ns() < deadline_ns) {
        taskYIELD();
        roused = task_doze_try();
    }
    return roused;
}

static inline bool task_doze_timed(uint64_t nanoseconds) {
    return task_doze_timed_abs(timer_now_ns() + nanoseconds);
}

// primitive-level task doze/rouse: may be used by individual libraries, so assumptions should not be made about
// whether other code may interfere with this.

static inline void local_rouse(thread_t task) {
    assert(task != NULL);
    BaseType_t result = xTaskNotifyGiveIndexed(task, NOTIFY_INDEX_LOCAL);
    assert(result == pdPASS);
}

static inline bool local_doze_try_raw(void) {
    return ulTaskNotifyTakeIndexed(NOTIFY_INDEX_LOCAL) > 0;
}

// does not actually block
static inline bool local_doze_try(thread_t task) {
    assert(task == task_get_current());
    return local_doze_try_raw();
}

static inline void local_doze(thread_t task) {
    assert(task == task_get_current());
    while (!local_doze_try_raw()) {
        taskYIELD();
    }
}

static inline bool local_doze_timed_abs(thread_t task, uint64_t deadline_ns) {
    assert(task == task_get_current());
    bool roused = local_doze_try_raw();
    while (!roused && timer_now_ns() < deadline_ns) {
        taskYIELD();
        roused = local_doze_try_raw();
    }
    return roused;
}

static inline bool local_doze_timed(thread_t task, uint64_t nanoseconds) {
    return local_doze_timed_abs(task, timer_now_ns() + nanoseconds);
}

#endif /* FSW_FREERTOS_HAL_THREAD_H */
