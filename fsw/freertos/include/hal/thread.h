#ifndef FSW_FREERTOS_HAL_THREAD_H
#define FSW_FREERTOS_HAL_THREAD_H

#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include <FreeRTOS.h>
#include <semphr.h>

#include <rtos/timer.h>
#include <fsw/debug.h>

enum {
    STACK_SIZE = 1000,
};

typedef enum {
    NOT_RESTARTABLE = 0,
    RESTARTABLE,
} restartable_t;

typedef struct thread_st {
    const char         *name;
    int                 priority;
    void              (*start_routine)(void*);
    void               *arg;
    restartable_t       restartable;
    bool                needs_restart;
    bool                hit_restart;
    TCB_t               tcb;
    StackType_t         preallocated_stack[STACK_SIZE];
} *thread_t;
typedef SemaphoreHandle_t semaphore_t;

// TODO: make the entrypoint parameter strongly typed like PROGRAM_INIT_PARAM
#define TASK_REGISTER(t_ident, t_name, t_priority, t_start, t_arg, t_restartable) \
    static_assert(t_priority < configMAX_PRIORITIES, "invalid priority"); \
    __attribute__((section(".tasktable"))) struct thread_st t_ident = {   \
        .name          = t_name,        \
        .priority      = t_priority,    \
        .start_routine = t_start,       \
        .arg           = t_arg,         \
        .restartable   = t_restartable, \
        .needs_restart = false,         \
        .hit_restart   = false,         \
        /* no need for anything for preallocated_ fields */   \
    }

static inline thread_t task_get_current(void) {
    TaskHandle_t handle = xTaskGetCurrentTaskHandle();
    assert(handle != NULL);
    thread_t task = (thread_t) ((uint8_t *) handle - offsetof(struct thread_st, tcb));
    assert(task != NULL && &task->tcb == handle);
    return task;
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
