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

static uint32_t sync_tasks = 0;
static pthread_barrier_t barrier;
static bool barrier_raised = false;

void epsync_enable(thread_t task) {
    assert(task->epsync_enabled == false);
    assert(!barrier_raised);
    task->epsync_enabled = true;
    sync_tasks++;
}

void epsync_register(void) {
    assert(!barrier_raised);
    if (sync_tasks > 0) {
        barrier_raised = true;
        THREAD_CHECK(pthread_barrier_init(&barrier, NULL, sync_tasks));
    }
}

void epsync_wait_next_epoch(void) {
    thread_t task = task_get_current();
    assert(task->epsync_enabled == true);
    assert(barrier_raised);
    if (EPLOCK_DEBUG) { debugf(TRACE, "epsync                - sleep start (task=%s)", task->name); }
    usleep(1000);
    if (!THREAD_CHECK_OK(pthread_barrier_wait(&barrier), PTHREAD_BARRIER_SERIAL_THREAD)) {
        if (EPLOCK_DEBUG || true) { debugf(TRACE, "epsync                - unblocked all (task=%s)", task->name); }
    }
    if (EPLOCK_DEBUG) { debugf(TRACE, "epsync                - sleep finish (task=%s)", task->name); }
}
