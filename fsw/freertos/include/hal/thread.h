#ifndef FSW_FREERTOS_HAL_THREAD_H
#define FSW_FREERTOS_HAL_THREAD_H

#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include <FreeRTOS.h>
#include <semphr.h>

#include <rtos/timer.h>
#include <fsw/debug.h>
#include <fsw/preprocessor.h>

typedef TCB_t *thread_t;
typedef SemaphoreHandle_t semaphore_t;

// array containing all .tasktable entries produced in TASK_REGISTER
// (this array is generated from fragments by the linker)
extern TCB_t tasktable_start[];
extern TCB_t tasktable_end[];

#define TASK_PROTO(t_ident) \
    extern TCB_t t_ident;

#define TASK_REGISTER(t_ident, t_name, t_priority, t_start, t_arg, t_restartable) \
    static_assert(t_priority < configMAX_PRIORITIES, "invalid priority");         \
    StackType_t t_ident ## _stack[RTOS_STACK_SIZE];                               \
    TCB_mut_t t_ident ## _mutable = {                                             \
        .pxTopOfStack    = NULL,                                                  \
        .needs_restart   = false,                                                 \
        .hit_restart     = false,                                                 \
        /* no init for lists here */                                              \
        .ulNotifiedValue = { 0 },                                                 \
        .ucNotifyState   = { 0 },                                                 \
    };                                                                            \
    __attribute__((section(".tasktable"))) TCB_t t_ident = {                      \
        .mut             = &t_ident ## _mutable,                                  \
        .start_routine   = PP_ERASE_TYPE(t_start, t_arg),                         \
        .start_arg       = t_arg,                                                 \
        .restartable     = t_restartable,                                         \
        .uxPriority      = t_priority,                                            \
        .pxStack         = t_ident ## _stack,                                     \
        .pcTaskName      = t_name,                                                \
    }

// TODO: fix write to uxPriority

static inline thread_t task_get_current(void) {
    TaskHandle_t handle = xTaskGetCurrentTaskHandle();
    assert(handle != NULL);
    return handle;
}

void task_suspend(void) __attribute__((noreturn));

static inline void task_delay(uint64_t nanoseconds) {
    vTaskDelay(timer_ns_to_ticks(nanoseconds));
}

static inline void task_delay_abs(uint64_t deadline_ns) {
    // TODO: should I use vTaskDelayUntil instead?
    vTaskDelay(timer_ticks_until_ns(deadline_ns));
    assert(timer_now_ns() >= deadline_ns);
}

static inline void task_rouse(thread_t task) {
    assert(task != NULL);
    BaseType_t result = xTaskNotifyGive(task);
    assert(result == pdPASS);
}

static inline void task_rouse_from_isr(thread_t task, BaseType_t *was_woken) {
    assert(task != NULL && was_woken != NULL);
    vTaskNotifyGiveFromISR(task, was_woken);
}

static inline void task_doze(void) {
    BaseType_t value;
    value = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    assert(value != 0);
}

// does not actually block
static inline bool task_doze_try(void) {
    return ulTaskNotifyTake(pdTRUE, 0) > 0;
}

static inline bool task_doze_timed(uint64_t nanoseconds) {
    return ulTaskNotifyTake(pdTRUE, timer_ns_to_ticks(nanoseconds)) > 0;
}

static inline bool task_doze_timed_abs(uint64_t deadline_ns) {
    return ulTaskNotifyTake(pdTRUE, timer_ticks_until_ns(deadline_ns)) > 0;
}

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
