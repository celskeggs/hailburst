#include <hal/thread.h>
#include <fsw/debug.h>

static void *thread_entry_wrapper(void *param) {
    assert(param != NULL);
    thread_t thread = (thread_t) param;
    assert(thread->start_routine != NULL);
    thread->start_routine(thread->start_parameter);
    return NULL;
}

extern struct thread_st tasktable_start[];
extern struct thread_st tasktable_end[];

void start_predef_threads(void) {
    debugf(DEBUG, "Starting %u predefined threads...", (uint32_t) (tasktable_end - tasktable_start));
    for (thread_t task = tasktable_start; task < tasktable_end; task++) {
        THREAD_CHECK(pthread_create(&task->thread, NULL, thread_entry_wrapper, task));
    }
    debugf(DEBUG, "Pre-registered threads started!");
}
