#include <inttypes.h>

#include <hal/debug.h>
#include <hal/eplock.h>
#include <hal/thread.h>

static bool initialized = false;

static pthread_key_t task_current_key;

extern struct thread_st tasktable_start[];
extern struct thread_st tasktable_end[];

static mutex_t        scheduling_lock;
static pthread_cond_t scheduling_cond;
static thread_t       scheduled_task;

thread_t task_get_current(void) {
    thread_t thread = (thread_t) pthread_getspecific(task_current_key);
    assert(thread != NULL && thread->thread == pthread_self());
    return thread;
}

static void task_wait_scheduled(void) {
    thread_t task = task_get_current();
    // TODO: avoid the thundering herd problem
    while (scheduled_task != task) {
        THREAD_CHECK(pthread_cond_wait(&scheduling_cond, &scheduling_lock));
    }
}

static void *thread_entry_wrapper(void *param) {
    assert(param != NULL);
    thread_t thread = (thread_t) param;

    // save a pointer to our own thread structure
    THREAD_CHECK(pthread_setspecific(task_current_key, thread));

    // yield before entering start routine, so that we only "go" when we're scheduled to
    mutex_lock(&scheduling_lock);
    task_wait_scheduled();
    mutex_unlock(&scheduling_lock);

    assertf(thread->start_routine != NULL, "no start routine for thread %s", thread->name);
    thread->start_routine(thread->start_parameter);

    debugf(WARNING, "Thread %s exited early", thread->name);
    task_become_independent();

    return NULL;
}

static void start_predef_threads(void) {
    assert(!initialized);

    mutex_init(&scheduling_lock);
    pthread_condattr_t attr;
    THREAD_CHECK(pthread_condattr_init(&attr));
    THREAD_CHECK(pthread_condattr_setclock(&attr, CLOCK_MONOTONIC));
    THREAD_CHECK(pthread_cond_init(&scheduling_cond, &attr));
    THREAD_CHECK(pthread_condattr_destroy(&attr));
    scheduled_task = NULL;

    initialized = true;

    // set up key for task_get_current
    THREAD_CHECK(pthread_key_create(&task_current_key, NULL));

    debugf(DEBUG, "Starting predefined threads...");
    for (thread_t task = tasktable_start; task < tasktable_end; task++) {
        THREAD_CHECK(pthread_create(&task->thread, NULL, thread_entry_wrapper, task));
    }
    debugf(DEBUG, "Predefined threads started!");
}

void task_yield(void) {
    mutex_lock(&scheduling_lock);
    thread_t task = task_get_current();
    assert(task->scheduler_independent == false);
    assert(scheduled_task == task);
    scheduled_task = NULL;
    pthread_cond_broadcast(&scheduling_cond);
    task_wait_scheduled();
    mutex_unlock(&scheduling_lock);
}

void task_become_independent(void) {
    thread_t task = task_get_current();
    mutex_lock(&scheduling_lock);
    assert(task->scheduler_independent == false);
    assert(scheduled_task == task);
    scheduled_task = NULL;
    task->scheduler_independent = true;
    pthread_cond_broadcast(&scheduling_cond);
    mutex_unlock(&scheduling_lock);
}

void task_become_dependent(void) {
    thread_t task = task_get_current();
    mutex_lock(&scheduling_lock);
    assert(task->scheduler_independent == true);
    task->scheduler_independent = false;
    task_wait_scheduled();
    mutex_unlock(&scheduling_lock);
}

static void task_schedule(schedule_entry_t entry) {
    // debugf(TRACE, "Scheduling: %s", entry.task->name);
    assert(entry.task != NULL);
    struct timespec deadline_ts;

    uint64_t start_ns = clock_timestamp_monotonic();
    uint64_t deadline_ns = start_ns + entry.nanos * 10;

    // this is possible because clock_timestamp_monotonic() uses CLOCK_MONOTONIC,
    // and we set our condition variable to CLOCK_MONOTONIC as well above.
    deadline_ts.tv_sec  = deadline_ns / CLOCK_NS_PER_SEC;
    deadline_ts.tv_nsec = deadline_ns % CLOCK_NS_PER_SEC;

    mutex_lock(&scheduling_lock);
    assert(scheduled_task == NULL);
    if (entry.task->scheduler_independent) {
        mutex_unlock(&scheduling_lock);
        return;
    }
    scheduled_task = entry.task;
    pthread_cond_broadcast(&scheduling_cond);
    while (scheduled_task != NULL) {
        int retcode = pthread_cond_timedwait(&scheduling_cond, &scheduling_lock, &deadline_ts);
        if (retcode == ETIMEDOUT) {
            debugf(WARNING, "task %s overran scheduling period: %" PRIu64 " > %u",
                   entry.task->name, clock_timestamp_monotonic() - start_ns, entry.nanos);
            while (scheduled_task != NULL) {
                THREAD_CHECK(pthread_cond_wait(&scheduling_cond, &scheduling_lock));
            }
            debugf(INFO, "task %s finally finished", entry.task->name);
        } else if (retcode != 0 && retcode != EINTR) {
            abortf("thread error: %d in task_schedule condition loop", retcode);
        }
    }
    mutex_unlock(&scheduling_lock);
}

void enter_scheduler(void) {
    start_predef_threads();

    for (;;) {
        // debugf(TRACE, "beginning cycle of schedule");
        for (uint32_t i = 0; i < task_scheduling_order_length; i++) {
            task_schedule(task_scheduling_order[i]);
        }
    }
}
