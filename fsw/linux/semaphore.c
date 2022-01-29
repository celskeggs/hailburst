#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <hal/clock.h>
#include <hal/debug.h>
#include <hal/thread.h>

// semaphores are created empty, such that an initial take will block
void semaphore_init(semaphore_t *sema) {
    assert(sema != NULL);

    mutex_init(&sema->mut);
    pthread_condattr_t attr;
    THREAD_CHECK(pthread_condattr_init(&attr));
    THREAD_CHECK(pthread_condattr_setclock(&attr, CLOCK_MONOTONIC));
    THREAD_CHECK(pthread_cond_init(&sema->cond, &attr));
    THREAD_CHECK(pthread_condattr_destroy(&attr));

    sema->is_available = false;
}

void semaphore_destroy(semaphore_t *sema) {
    assert(sema != NULL);
    THREAD_CHECK(pthread_cond_destroy(&sema->cond));
    mutex_destroy(&sema->mut);
}

void semaphore_take(semaphore_t *sema) {
    assert(sema != NULL);
    mutex_lock(&sema->mut);
    while (!sema->is_available) {
        THREAD_CHECK(pthread_cond_wait(&sema->cond, &sema->mut));
    }
    assert(sema->is_available == true);
    sema->is_available = false;
    mutex_unlock(&sema->mut);
}

// returns true if taken, false if not available
bool semaphore_take_try(semaphore_t *sema) {
    assert(sema != NULL);
    bool taken;
    mutex_lock(&sema->mut);
    taken = sema->is_available;
    sema->is_available = false;
    mutex_unlock(&sema->mut);
    return taken;
}

// returns true if taken, false if timed out
bool semaphore_take_timed(semaphore_t *sema, uint64_t nanoseconds) {
    return semaphore_take_timed_abs(sema, clock_timestamp_monotonic() + nanoseconds);
}

// returns true if taken, false if timed out
bool semaphore_take_timed_abs(semaphore_t *sema, uint64_t deadline_ns) {
    assert(sema != NULL);
    struct timespec deadline_ts;

    // this is possible because clock_timestamp_monotonic() uses CLOCK_MONOTONIC_RAW,
    // and we set our condition variable to CLOCK_MONOTONIC_RAW as well above.
    deadline_ts.tv_sec  = deadline_ns / CLOCK_NS_PER_SEC;
    deadline_ts.tv_nsec = deadline_ns % CLOCK_NS_PER_SEC;

    mutex_lock(&sema->mut);
    while (!sema->is_available) {
        int retcode = pthread_cond_timedwait(&sema->cond, &sema->mut, &deadline_ts);
        if (retcode == ETIMEDOUT) {
            mutex_unlock(&sema->mut);
            assert(clock_timestamp_monotonic() >= deadline_ns);
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

bool semaphore_give(semaphore_t *sema) {
    assert(sema != NULL);
    bool given = false;
    mutex_lock(&sema->mut);
    if (!sema->is_available) {
        sema->is_available = true;
        THREAD_CHECK(pthread_cond_signal(&sema->cond));
        given = true;
    }
    mutex_unlock(&sema->mut);
    return given;
}
