#ifndef FSW_VIVID_HAL_THREAD_H
#define FSW_VIVID_HAL_THREAD_H

#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include <FreeRTOS.h>
#include <task.h>

#include <rtos/config.h>
#include <rtos/replicate.h>
#include <hal/timer.h>
#include <hal/atomic.h>
#include <hal/debug.h>
#include <hal/preprocessor.h>

typedef TCB_t *thread_t;

macro_define(TASK_PROTO, t_ident) {
    extern TCB_t t_ident;
}

macro_define(TASK_REGISTER, t_ident, t_start, t_arg, t_restartable) {
    StackType_t symbol_join(t_ident, stack)[RTOS_STACK_SIZE];
    TCB_mut_t symbol_join(t_ident, mutable) = {
        .pxTopOfStack        = NULL,
        .needs_start         = true,
        .hit_restart         = false,
        .recursive_exception = false,
        .clip_running        = false,
        .clip_next_tick      = 0,
    };
#if ( VIVID_REPLICATE_TASK_CODE == 1 )
    REPLICATE_OBJECT_CODE(t_start, symbol_join(t_ident, start_fn));
#endif
    TCB_t t_ident = {
        .mut             = &symbol_join(t_ident, mutable),
#if ( VIVID_REPLICATE_TASK_CODE == 1 )
        .start_routine   = PP_ERASE_TYPE(symbol_join(t_ident, start_fn), t_arg),
#else /* VIVID_REPLICATE_TASK_CODE == 0 */
        .start_routine   = PP_ERASE_TYPE(t_start, t_arg),
#endif
        .start_arg       = (void *) t_arg,
        .restartable     = t_restartable,
        .pxStack         = symbol_join(t_ident, stack),
        .pcTaskName      = symbol_str(t_ident),
    }
}

macro_define(CLIP_REGISTER, c_ident, c_play, c_arg) {
    TASK_REGISTER(c_ident, c_play, c_arg, RESTART_ON_RESCHEDULE)
}

macro_define(TASK_SCHEDULE, t_ident, t_micros) {
    { .task = &(t_ident), .nanos = (t_micros) * 1000 },
}

macro_define(CLIP_SCHEDULE, c_ident, c_micros) {
    TASK_SCHEDULE(c_ident, c_micros)
}

#define TASK_SCHEDULING_ORDER(...)                                                                                    \
    const schedule_entry_t task_scheduling_order[] = {                                                                \
        __VA_ARGS__                                                                                                   \
    };                                                                                                                \
    const uint32_t task_scheduling_order_length =                                                                     \
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

static inline void task_yield(void) {
    uint64_t loads = schedule_loads;
    assert((arm_get_cpsr() & ARM_CPSR_MASK_INTERRUPTS) == 0);
    do {
        asm volatile("WFI");
    } while (loads == schedule_loads);
}

static inline uint32_t task_tick_index(void) {
    return (uint32_t) schedule_ticks;
}

void task_suspend(void) __attribute__((noreturn));

static inline local_time_t timer_epoch_ns(void) {
    return schedule_epoch_start;
}

#endif /* FSW_VIVID_HAL_THREAD_H */
