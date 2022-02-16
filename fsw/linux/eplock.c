#include <hal/eplock.h>

void eplock_init(eplock_t *lock) {
    if (EPLOCK_DEBUG) { debugf(TRACE, "eplock %p - initialize", lock); }
    mutex_init(&lock->mutex);
    pthread_condattr_t attr;
    THREAD_CHECK(pthread_condattr_init(&attr));
    THREAD_CHECK(pthread_condattr_setclock(&attr, CLOCK_MONOTONIC));
    THREAD_CHECK(pthread_cond_init(&lock->cond, &attr));
    THREAD_CHECK(pthread_condattr_destroy(&attr));
}
