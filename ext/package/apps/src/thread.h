#ifndef APP_THREAD_H
#define APP_THREAD_H

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

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

#define mutex_init(x)    THREAD_CHECK(pthread_mutex_init((x), NULL))
#define mutex_destroy(x) THREAD_CHECK(pthread_mutex_destroy(x))
#define mutex_lock(x)    THREAD_CHECK(pthread_mutex_lock(x))
#define mutex_unlock(x)  THREAD_CHECK(pthread_mutex_unlock(x))

#define cond_init(x)      THREAD_CHECK(pthread_cond_init((x), NULL))
#define cond_destroy(x)   THREAD_CHECK(pthread_cond_destroy(x))
#define cond_broadcast(x) THREAD_CHECK(pthread_cond_broadcast(x))
#define cond_wait(c, m)   THREAD_CHECK(pthread_cond_wait((c), (m)))

#define thread_create(x, entrypoint, param) THREAD_CHECK(pthread_create((x), NULL, (entrypoint), (param)))
#define thread_join(x)                      THREAD_CHECK(pthread_join((x), NULL))
#define thread_cancel(x)                    THREAD_CHECK(pthread_cancel(x))
#define thread_time_now(x)                  THREAD_CHECK(clock_gettime(CLOCK_REALTIME, (x)))
#define thread_join_timed(x, t)        THREAD_CHECK_OK(pthread_timedjoin_np((x), NULL, (t)), ETIMEDOUT)

#endif /* APP_THREAD_H */
