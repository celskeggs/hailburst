#ifndef APP_THREAD_H
#define APP_THREAD_H

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

enum {
    NS_PER_SEC = 1000 * 1000 * 1000,
};

#define THREAD_CHECK(x) (thread_check((x), #x))
#define THREAD_CHECK_OK(x, fm) (thread_check_ok((x), #x, (fm)))

static inline void thread_check(int fail, const char *note) {
    if (fail != 0) {
        fprintf(stderr, "thread error: %d in %s\n", fail, note);
        exit(1);
    }
}

static inline bool thread_check_ok(int fail, const char *note, int false_marker) {
    if (fail == 0) {
        return true;
    } else if (fail == false_marker) {
        return false;
    } else {
        fprintf(stderr, "thread error: %d in %s\n", fail, note);
        exit(1);
    }
}

static inline void cond_timedwait_impl(pthread_cond_t *cond, pthread_mutex_t *mutex, uint64_t nanoseconds, const char *nc, const char *nm, const char *na) {
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
        exit(1);
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

#define thread_create(x, entrypoint, param) THREAD_CHECK(pthread_create((x), NULL, (entrypoint), (param)))
#define thread_join(x)                      THREAD_CHECK(pthread_join((x), NULL))
#define thread_cancel(x)                    THREAD_CHECK(pthread_cancel(x))
#define thread_time_now(x)                  THREAD_CHECK(clock_gettime(CLOCK_REALTIME, (x)))
#define thread_join_timed(x, t)             THREAD_CHECK_OK(pthread_timedjoin_np((x), NULL, (t)), ETIMEDOUT)
#define thread_disable_cancellation()       THREAD_CHECK(pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL))
#define thread_enable_cancellation()        THREAD_CHECK(pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL))
#define thread_testcancel()                 (pthread_testcancel())

#endif /* APP_THREAD_H */
