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

#include <hal/clock.h>
#include <hal/preprocessor.h>

#define THREAD_CHECK(x) (thread_check((x), #x))
#define THREAD_CHECK_OK(x, fm) (thread_check_ok((x), #x, (fm)))

typedef pthread_mutex_t mutex_t;

// although there are semaphores available under POSIX, they are counting semaphores, and not binary semaphores.
typedef struct {
    mutex_t        mut;
    pthread_cond_t cond;
    bool           is_available;
} semaphore_t;

typedef struct thread_st {
    void (*start_routine)(void *);
    void *start_parameter;
    pthread_t thread;
    semaphore_t top_rouse;
    semaphore_t local_rouse;
} __attribute__((__aligned__(16))) *thread_t; // alignment must be specified for x86_64 compatibility

thread_t task_get_current(void);

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

static inline void task_delay(uint64_t nanoseconds) {
    usleep((nanoseconds + 999) / 1000);
}

static inline void task_delay_abs(uint64_t deadline_ns) {
    int64_t remain = deadline_ns - clock_timestamp_monotonic();
    if (remain > 0) {
        task_delay(remain);
    }
    assert(clock_timestamp_monotonic() >= deadline_ns);
}

#define mutex_init(x)     THREAD_CHECK(pthread_mutex_init((x), NULL))
#define mutex_destroy(x)  THREAD_CHECK(pthread_mutex_destroy(x))
#define mutex_lock(x)     THREAD_CHECK(pthread_mutex_lock(x))
#define mutex_lock_try(x) THREAD_CHECK_OK(pthread_mutex_trylock(x), EBUSY)
#define mutex_unlock(x)   THREAD_CHECK(pthread_mutex_unlock(x))

extern void start_predef_threads(void);

#define TASK_PROTO(t_ident) \
    extern struct thread_st t_ident;

// name and restartable go unused on POSIX; these are only used on FreeRTOS
#define TASK_REGISTER(t_ident, t_name, t_start, t_arg, t_restartable)                                                 \
    __attribute__((section("tasktable"))) struct thread_st t_ident = {                                                \
        .start_routine = PP_ERASE_TYPE(t_start, t_arg),                                                               \
        .start_parameter = (void *) (t_arg),                                                                          \
    }

// ignore scheduling
#define TASK_SCHEDULE(t_ident)     /* nothing */
#define TASK_SCHEDULING_ORDER(...) /* nothing */

#define SEMAPHORE_REGISTER(s_ident) \
    semaphore_t s_ident; \
    PROGRAM_INIT_PARAM(STAGE_RAW, semaphore_init, s_ident, &s_ident);

// semaphores are created empty, such that an initial take will block
void semaphore_init(semaphore_t *sema);
void semaphore_destroy(semaphore_t *sema);

void semaphore_take(semaphore_t *sema);
// returns true if taken, false if not available
bool semaphore_take_try(semaphore_t *sema);
// returns true if taken, false if timed out
bool semaphore_take_timed(semaphore_t *sema, uint64_t nanoseconds);
// returns true if taken, false if timed out
bool semaphore_take_timed_abs(semaphore_t *sema, uint64_t deadline_ns);
bool semaphore_give(semaphore_t *sema);

// top-level task doze/rouse: should only be used by the code that defines a task, not intermediate libraries

static inline void task_rouse(thread_t task) {
    assert(task != NULL);
    (void) semaphore_give(&task->top_rouse);
}

static inline void task_doze(void) {
    semaphore_take(&task_get_current()->top_rouse);
}

// does not actually block
static inline bool task_doze_try(void) {
    return semaphore_take_try(&task_get_current()->top_rouse);
}

static inline bool task_doze_timed(uint64_t nanoseconds) {
    return semaphore_take_timed(&task_get_current()->top_rouse, nanoseconds);
}

static inline bool task_doze_timed_abs(uint64_t deadline_ns) {
    return semaphore_take_timed_abs(&task_get_current()->top_rouse, deadline_ns);
}

// primitive-level task doze/rouse: may be used by individual libraries, so assumptions should not be made about
// whether other code may interfere with this.

static inline void local_rouse(thread_t task) {
    assert(task != NULL);
    (void) semaphore_give(&task->local_rouse);
}

static inline void local_doze(thread_t task) {
    assert(task == task_get_current());
    semaphore_take(&task_get_current()->local_rouse);
}

// does not actually block
static inline bool local_doze_try(thread_t task) {
    assert(task == task_get_current());
    return semaphore_take_try(&task_get_current()->local_rouse);
}

static inline bool local_doze_timed(thread_t task, uint64_t nanoseconds) {
    assert(task == task_get_current());
    return semaphore_take_timed(&task_get_current()->local_rouse, nanoseconds);
}

static inline bool local_doze_timed_abs(thread_t task, uint64_t deadline_ns) {
    assert(task == task_get_current());
    return semaphore_take_timed_abs(&task_get_current()->local_rouse, deadline_ns);
}

#endif /* FSW_LINUX_HAL_THREAD_H */
