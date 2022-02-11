#ifndef FSW_FREERTOS_HAL_THREAD_H
#define FSW_FREERTOS_HAL_THREAD_H

#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include <FreeRTOS.h>
#include <task.h>

#include <rtos/replicate.h>
#include <rtos/timer.h>
#include <hal/atomic.h>
#include <hal/debug.h>
#include <hal/preprocessor.h>

typedef TCB_t *thread_t;

#define TASK_PROTO(t_ident) \
    extern TCB_t t_ident;

#define TASK_REGISTER_INNER(t_ident, t_name, t_start, t_arg, t_restartable)       \
    StackType_t symbol_join(t_ident, stack)[RTOS_STACK_SIZE];                     \
    TCB_mut_t symbol_join(t_ident, mutable) = {                                   \
        .pxTopOfStack    = NULL,                                                  \
        .needs_start     = true,                                                  \
        .hit_restart     = false,                                                 \
        .recursive_exception = false,                                             \
        .roused_task     = 0,                                                     \
        .roused_local    = 0,                                                     \
    };                                                                            \
    __attribute__((section("tasktable"))) TCB_t t_ident = {                       \
        .mut             = &symbol_join(t_ident, mutable),                        \
        .start_routine   = PP_ERASE_TYPE(t_start, t_arg),                         \
        .start_arg       = (void *) t_arg,                                        \
        .restartable     = t_restartable,                                         \
        .pxStack         = symbol_join(t_ident, stack),                           \
        .pcTaskName      = t_name,                                                \
    }

#if ( VIVID_REPLICATE_TASK_CODE == 1 )
#define TASK_REGISTER(t_ident, t_name, t_start, t_arg, t_restartable)                                                 \
    REPLICATE_OBJECT_CODE(t_start, symbol_join(t_ident, start_fn));                                                   \
    TASK_REGISTER_INNER(t_ident, t_name, symbol_join(t_ident, start_fn), t_arg, t_restartable)
#else /* VIVID_REPLICATE_TASK_CODE == 0 */
#define TASK_REGISTER(t_ident, t_name, t_start, t_arg, t_restartable)                                                 \
    TASK_REGISTER_INNER(t_ident, t_name, t_start, t_arg, t_restartable)
#endif

#define TASK_SCHEDULE(t_ident, t_micros)                                          \
    { .task = &(t_ident), .nanos = (t_micros) * 1000 },

#define TASK_SCHEDULING_ORDER(...)                                                \
    const schedule_entry_t task_scheduling_order[] = {                            \
        __VA_ARGS__                                                               \
    };                                                                            \
    const uint32_t task_scheduling_order_length =                                 \
        sizeof(task_scheduling_order) / sizeof(task_scheduling_order[0])

static inline bool scheduler_has_started(void) {
    return pxCurrentTCB != NULL;
}

static inline thread_t task_get_current(void) {
    TaskHandle_t handle = pxCurrentTCB;
    assert(handle != NULL);
    return handle;
}

static inline const char *task_get_name(thread_t task) {
    assert(task != NULL);
    return task->pcTaskName;
}

static inline void taskYIELD(void) {
    assert((arm_get_cpsr() & ARM_CPSR_MASK_INTERRUPTS) == 0);
    asm volatile("WFI");
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
    atomic_store(task->mut->roused_task, 1);
}

// does not block
static inline bool task_doze_try(void) {
    return atomic_fetch_and(task_get_current()->mut->roused_task, 0) != 0;
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
    atomic_store(task->mut->roused_local, 1);
}

static inline bool local_doze_try_raw(void) {
    return atomic_fetch_and(task_get_current()->mut->roused_local, 0) != 0;
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
