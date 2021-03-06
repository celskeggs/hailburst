#ifndef FSW_LINUX_HAL_THREAD_H
#define FSW_LINUX_HAL_THREAD_H

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>
#include <unistd.h>

#include <hal/atomic.h>
#include <hal/timer.h>
#include <hal/preprocessor.h>

#define THREAD_CHECK(x) (thread_check((x), #x))
#define THREAD_CHECK_OK(x, fm) (thread_check_ok((x), #x, (fm)))

typedef pthread_mutex_t mutex_t;

typedef struct thread_st {
    const char *name;
    void (*start_routine)(void *);
    void *start_parameter;
    pthread_t thread;
    bool scheduler_independent; // used during IO waits
    pthread_cond_t sched_cond;
} __attribute__((__aligned__(16))) *thread_t; // alignment must be specified for x86_64 compatibility

thread_t task_get_current(void);
void task_yield(void);
uint32_t task_tick_index(void);
void task_become_independent(void);
void task_become_dependent(void);

static inline const char *task_get_name(thread_t task) {
    assert(task != NULL);
    return task->name;
}

static inline void thread_check(int fail, const char *note) {
    if (fail != 0) {
        fprintf(stderr, "thread error: %d in %s\n", fail, note);
        abort();
    }
}

static inline bool thread_check_ok(int fail, const char *note, int false_marker) {
    if (fail == 0) {
        return true;
    } else if (fail == false_marker) {
        return false;
    } else {
        fprintf(stderr, "thread error: %d in %s\n", fail, note);
        abort();
    }
}

static inline void task_delay_abs(local_time_t deadline_ns) {
    while (timer_now_ns() < deadline_ns) {
        task_yield();
    }
}

static inline void task_delay(local_time_t nanoseconds) {
    task_delay_abs(timer_now_ns() + nanoseconds);
}

extern local_time_t schedule_epoch_start;

static inline local_time_t timer_epoch_ns(void) {
    return schedule_epoch_start;
}

#define mutex_init(x)     THREAD_CHECK(pthread_mutex_init((x), NULL))
#define mutex_destroy(x)  THREAD_CHECK(pthread_mutex_destroy(x))
#define mutex_lock(x)     THREAD_CHECK(pthread_mutex_lock(x))
#define mutex_lock_try(x) THREAD_CHECK_OK(pthread_mutex_trylock(x), EBUSY)
#define mutex_unlock(x)   THREAD_CHECK(pthread_mutex_unlock(x))

extern void enter_scheduler(void);

#define TASK_PROTO(t_ident) \
    extern struct thread_st t_ident;

// 'restartable' goes unused on POSIX; it is only used on Vivid
macro_define(TASK_REGISTER, t_ident, t_start, t_arg, t_restartable) {
    __attribute__((section("tasktable")))
    __attribute__((__aligned__(16)))
    struct thread_st t_ident = {
        .name = symbol_str(t_ident),
        .start_routine = PP_ERASE_TYPE(t_start, t_arg),
        .start_parameter = (void *) (t_arg),
        .scheduler_independent = false,
    }
}

typedef struct {
    thread_t task;
    uint32_t nanos;
} schedule_entry_t;

extern const schedule_entry_t task_scheduling_order[];
extern const uint32_t         task_scheduling_order_length;

macro_define(TASK_SCHEDULE, t_ident, t_micros) {
    { .task = &(t_ident), .nanos = (t_micros) * 1000 },
}

macro_block_define(SCHEDULE_PARTITION_ORDER, body) {
    const schedule_entry_t task_scheduling_order[] = {
        body
    };
    const uint32_t task_scheduling_order_length = sizeof(task_scheduling_order) / sizeof(task_scheduling_order[0]);
}

#endif /* FSW_LINUX_HAL_THREAD_H */
