#include <inttypes.h>

#include <hal/debug.h>
#include <hal/thread.h>
#include <hal/timer.h>

//#define SCHED_DEBUG

static bool initialized = false;

static pthread_key_t task_current_key;

extern struct thread_st tasktable_start[];
extern struct thread_st tasktable_end[];

static mutex_t        scheduling_lock;
static thread_t       scheduled_task;
static uint32_t       schedule_index;

thread_t task_get_current(void) {
    thread_t thread = (thread_t) pthread_getspecific(task_current_key);
    assert(thread != NULL && thread->thread == pthread_self());
    return thread;
}

static void task_wait_scheduled(void) {
    thread_t task = task_get_current();
    // TODO: avoid the thundering herd problem
    while (scheduled_task != task) {
        THREAD_CHECK(pthread_cond_wait(&task->sched_cond, &scheduling_lock));
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
    scheduled_task = NULL;

    initialized = true;

    // set up key for task_get_current
    THREAD_CHECK(pthread_key_create(&task_current_key, NULL));

    debugf(DEBUG, "Starting predefined threads...");
    for (thread_t task = tasktable_start; task < tasktable_end; task++) {
        THREAD_CHECK(pthread_cond_init(&task->sched_cond, &attr));
        THREAD_CHECK(pthread_create(&task->thread, NULL, thread_entry_wrapper, task));
    }
    debugf(DEBUG, "Predefined threads started!");
    THREAD_CHECK(pthread_condattr_destroy(&attr));
}

void task_yield(void) {
    mutex_lock(&scheduling_lock);
    thread_t task = task_get_current();
    assert(task->scheduler_independent == false);
    assert(scheduled_task == task);
    scheduled_task = NULL;
    pthread_cond_broadcast(&task->sched_cond);
    task_wait_scheduled();
    mutex_unlock(&scheduling_lock);
}

uint32_t task_tick_index(void) {
    return schedule_index;
}

void task_become_independent(void) {
    thread_t task = task_get_current();
    mutex_lock(&scheduling_lock);
    assert(task->scheduler_independent == false);
    assert(scheduled_task == task);
    scheduled_task = NULL;
    task->scheduler_independent = true;
    pthread_cond_broadcast(&task->sched_cond);
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

    THREAD_CHECK(clock_gettime(CLOCK_MONOTONIC, &deadline_ts));
    deadline_ts.tv_sec += 1;

    mutex_lock(&scheduling_lock);
    assert(scheduled_task == NULL);
    if (entry.task->scheduler_independent) {
        mutex_unlock(&scheduling_lock);
        return;
    }
    scheduled_task = entry.task;
    pthread_cond_broadcast(&scheduled_task->sched_cond);
    while (scheduled_task != NULL) {
        int retcode = pthread_cond_timedwait(&scheduled_task->sched_cond, &scheduling_lock, &deadline_ts);
        if (retcode == ETIMEDOUT) {
            // went an entire second without yielding! we assume this indicates a malfunction, rather than a delay
            debugf(WARNING, "task %s overran scheduling period", entry.task->name);
            THREAD_CHECK(pthread_cond_wait(&scheduled_task->sched_cond, &scheduling_lock));
        } else if (retcode != 0 && retcode != EINTR) {
            abortf("thread error: %d in task_schedule condition loop", retcode);
        }
    }
    mutex_unlock(&scheduling_lock);

    // uint64_t total_time = timer_now_ns() - start_ns;
    // if (total_time > entry.nanos) {
    //     debugf(TRACE, "task %s consumed %" PRIu64 " > %u", entry.task->name, total_time, entry.nanos);
    // }
}

void enter_scheduler(void) {
    start_predef_threads();

    uint64_t total = 0;
    for (uint32_t i = 0; i < task_scheduling_order_length; i++) {
        total += task_scheduling_order[i].nanos;
    }

    uint64_t last = timer_now_ns();
    for (;;) {
        // debugf(TRACE, "beginning cycle of schedule");
        for (uint32_t i = 0; i < task_scheduling_order_length; i++) {
            task_schedule(task_scheduling_order[i]);
        }
        uint64_t here = timer_now_ns();
        if (here - last > total) {
            debugf(TRACE, "Epoch too long:   %" PRIu64 " > %" PRIu64, here - last, total);
        } else {
#ifdef SCHED_DEBUG
            debugf(TRACE, "Epoch acceptable: %" PRIu64 " < %" PRIu64, here - last, total);
#endif
        }
        last = here;
        schedule_index++;
    }
}
