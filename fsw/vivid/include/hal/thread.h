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

// clip wrapper that runs within the execution context
void clip_play_direct(void (*entrypoint)(void*)) __attribute__((noreturn));
// scheduler call to enter the execution context and run clip_play_direct
void clip_enter_context(void (*entrypoint)(void*)) __attribute__((noreturn));

macro_define(CLIP_REGISTER, c_ident, c_play, c_arg) {
#if ( VIVID_REPLICATE_TASK_CODE == 1 )
    // We need to define a single function that references both clip_enter_context AND the entrypoint so that we can
    // avoid replicating the two separately.
    void symbol_join(c_ident, enter_context_unreplicated)(void) {
        clip_enter_context(PP_ERASE_TYPE(c_play, c_arg));
    }
    REPLICATE_OBJECT_CODE(symbol_join(c_ident, enter_context_unreplicated), symbol_join(c_ident, enter_context));
#else /* VIVID_REPLICATE_TASK_CODE == 0 */
    void symbol_join(c_ident, enter_context)(void) {
        clip_enter_context(PP_ERASE_TYPE(c_play, c_arg));
    }
#endif
    TCB_mut_t symbol_join(c_ident, mutable) = {
        .needs_start         = true,
        .hit_restart         = false,
        .recursive_exception = false,
        .clip_running        = false,
        .clip_next_tick      = 0,
    };
    TCB_t c_ident = {
        .mut             = &symbol_join(c_ident, mutable),
        .enter_context   = symbol_join(c_ident, enter_context),
        .start_arg       = (void *) c_arg,
        .pcTaskName      = symbol_str(c_ident),
    }
}

macro_define(CLIP_SCHEDULE, c_ident, c_micros) {
    { .task = &(c_ident), .nanos = (c_micros) * 1000 },
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
    assert((arm_get_cpsr() & ARM_CPSR_MASK_INTERRUPTS) == 0);
    asm volatile("WFI");
    abortf("should never return from WFI since all non-timer interrupts are masked");
}

static inline uint32_t task_tick_index(void) {
    return (uint32_t) schedule_ticks;
}

void task_suspend(void) __attribute__((noreturn));

static inline local_time_t timer_epoch_ns(void) {
    return schedule_epoch_start;
}

#endif /* FSW_VIVID_HAL_THREAD_H */
