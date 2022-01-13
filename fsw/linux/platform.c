#include <hal/thread.h>
#include <fsw/debug.h>

static bool initialized = false;

static pthread_key_t task_current_key;

thread_t task_get_current(void) {
    thread_t thread = (thread_t) pthread_getspecific(task_current_key);
    assert(thread != NULL && thread->thread == pthread_self());
    return thread;
}

static void *thread_entry_wrapper(void *param) {
    assert(param != NULL);
    thread_t thread = (thread_t) param;

    // save a pointer to our own thread structure
    THREAD_CHECK(pthread_setspecific(task_current_key, thread));

    assert(thread->start_routine != NULL);
    thread->start_routine(thread->start_parameter);
    return NULL;
}

extern struct thread_st tasktable_start[];
extern struct thread_st tasktable_end[];

void start_predef_threads(void) {
    assert(!initialized);
    initialized = true;

    // set up key for task_get_current
    THREAD_CHECK(pthread_key_create(&task_current_key, NULL));

    debugf(DEBUG, "Preparing rouse semaphores for %u predefined threads...",
                    (uint32_t) (tasktable_end - tasktable_start));
    for (thread_t task = tasktable_start; task < tasktable_end; task++) {
        semaphore_init(&task->top_rouse);
        semaphore_init(&task->local_rouse);
    }
    debugf(DEBUG, "Starting predefined threads...");
    for (thread_t task = tasktable_start; task < tasktable_end; task++) {
        THREAD_CHECK(pthread_create(&task->thread, NULL, thread_entry_wrapper, task));
    }
    debugf(DEBUG, "Predefined threads started!");
}
