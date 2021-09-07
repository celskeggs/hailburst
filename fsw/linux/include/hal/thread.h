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

enum {
    NS_PER_SEC = 1000 * 1000 * 1000,
};

#define THREAD_CHECK(x) (thread_check((x), #x))
#define THREAD_CHECK_OK(x, fm) (thread_check_ok((x), #x, (fm)))

typedef pthread_t       thread_t;
typedef pthread_mutex_t mutex_t;
typedef pthread_cond_t  cond_t;
// although there are semaphores available under POSIX, they are counting semaphores, and not binary semaphores.
typedef struct {
    mutex_t mut;
    cond_t  cond;
    bool    is_available;
} semaphore_t;

typedef semaphore_t *wakeup_t;

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

static inline void cond_timedwait_impl(cond_t *cond, mutex_t *mutex, uint64_t nanoseconds, const char *nc, const char *nm, const char *na) {
    struct timeval now;
    struct timespec timeout;
    int retcode;

    gettimeofday(&now, NULL);

    nanoseconds += now.tv_usec * 1000;
    timeout.tv_sec = now.tv_sec + nanoseconds / NS_PER_SEC;
    timeout.tv_nsec = nanoseconds % NS_PER_SEC;

    retcode = pthread_cond_timedwait(cond, mutex, &timeout);
    if (retcode != 0 && retcode != ETIMEDOUT && retcode != EINTR) {
        fprintf(stderr, "thread error: %d in cond_timedwait(%s, %s, %s)\n", retcode, nc, nm, na);
        abort();
    }
}

static inline void thread_cancel_impl(thread_t thread, const char *nt) {
    int err = pthread_cancel(thread);
    if (err != 0 && err != ESRCH) {
        fprintf(stderr, "thread error: %d in thread_cancel(%s)\n", err, nt);
        abort();
    }
}

#define mutex_init(x)    THREAD_CHECK(pthread_mutex_init((x), NULL))
#define mutex_destroy(x) THREAD_CHECK(pthread_mutex_destroy(x))
#define mutex_lock(x)    THREAD_CHECK(pthread_mutex_lock(x))
#define mutex_unlock(x)  THREAD_CHECK(pthread_mutex_unlock(x))

#define cond_init(x)            THREAD_CHECK(pthread_cond_init((x), NULL))
#define cond_destroy(x)         THREAD_CHECK(pthread_cond_destroy(x))
#define cond_broadcast(x)       THREAD_CHECK(pthread_cond_broadcast(x))
#define cond_wait(c, m)         THREAD_CHECK(pthread_cond_wait((c), (m)))
#define cond_timedwait(c, m, a) (cond_timedwait_impl((c), (m), (a), #c, #m, #a))

// name and priority go unused on POSIX; these are only used on FreeRTOS
#define thread_create(x, name, priority, entrypoint, param) THREAD_CHECK(pthread_create((x), NULL, (entrypoint), (param)))
#define thread_join(x)                                      THREAD_CHECK(pthread_join((x), NULL))
#define thread_cancel(x)                                    (thread_cancel_impl((x), #x))
#define thread_time_now(x)                                  THREAD_CHECK(clock_gettime(CLOCK_REALTIME, (x)))
#define thread_join_timed(x, t)                             THREAD_CHECK_OK(pthread_timedjoin_np((x), NULL, (t)), ETIMEDOUT)
#define thread_disable_cancellation()                       THREAD_CHECK(pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL))
#define thread_enable_cancellation()                        THREAD_CHECK(pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL))
#define thread_testcancel()                                 (pthread_testcancel())

// semaphores are created empty, such that an initial take will block
static inline void semaphore_init(semaphore_t *sema) {
    assert(sema != NULL);
    mutex_init(&sema->mut);
    cond_init(&sema->cond);
    sema->is_available = false;
}

static inline void semaphore_destroy(semaphore_t *sema) {
    assert(sema != NULL);
    cond_destroy(&sema->cond);
    mutex_destroy(&sema->mut);
}

static inline void semaphore_take(semaphore_t *sema) {
    assert(sema != NULL);
    mutex_lock(&sema->mut);
    while (!sema->is_available) {
        cond_wait(&sema->cond, &sema->mut);
    }
    assert(sema->is_available == true);
    sema->is_available = false;
    mutex_unlock(&sema->mut);
}

// returns true if taken, false if timed out
static inline bool semaphore_take_timed(semaphore_t *sema, uint64_t nanoseconds) {
    struct timeval now;
    struct timespec timeout;
    int retcode;

    assert(sema != NULL);

    gettimeofday(&now, NULL);

    nanoseconds += now.tv_usec * 1000;
    timeout.tv_sec = now.tv_sec + nanoseconds / NS_PER_SEC;
    timeout.tv_nsec = nanoseconds % NS_PER_SEC;

    mutex_lock(&sema->mut);
    while (!sema->is_available) {
        retcode = pthread_cond_timedwait(&sema->cond, &sema->mut, &timeout);
        if (retcode == ETIMEDOUT) {
            mutex_unlock(&sema->mut);
            return false;
        } else if (retcode != 0 && retcode != EINTR) {
            fprintf(stderr, "thread error: %d in semaphore_take_timed\n", retcode);
            abort();
        }
    }
    assert(sema->is_available == true);
    sema->is_available = false;
    mutex_unlock(&sema->mut);
    return true;
}

static inline bool semaphore_give(semaphore_t *sema) {
    assert(sema != NULL);
    bool given = false;
    mutex_lock(&sema->mut);
    if (!sema->is_available) {
        sema->is_available = true;
        pthread_cond_signal(&sema->cond);
        given = true;
    }
    mutex_unlock(&sema->mut);
    return given;
}

// not for generic code; only for internal Linux wakeup code implementation
static inline void semaphore_reset_linuxonly(semaphore_t *sema) {
    mutex_lock(&sema->mut);
    sema->is_available = false;
    mutex_unlock(&sema->mut);
}

extern wakeup_t wakeup_open(void);

static inline void wakeup_take(wakeup_t wakeup) {
    semaphore_take(wakeup);
}

// returns true if taken, false if timed out
// NOTE: on a timeout, the caller MUST ensure that the wakeup is never given in the future!
// (It is OK for the wakeup to be given immediately after return, as long as the thread calling wakeup_take_timed does
//  not perform any operations that could potentially use the thread-specific notification pathway.)
static inline bool wakeup_take_timed(wakeup_t wakeup, uint64_t nanoseconds) {
    return semaphore_take_timed(wakeup, nanoseconds);
}

static inline void wakeup_give(wakeup_t wakeup) {
    semaphore_give(wakeup);
}

#endif /* FSW_LINUX_HAL_THREAD_H */
