#include <hal/thread.h>
#include <fsw/debug.h>

static void *thread_entry_wrapper(void *param) {
    assert(param != NULL);
    thread_t *thread = (thread_t *) param;
    assert(thread->start_routine != NULL);
    thread->start_routine(thread->start_parameter);
    return NULL;
}

void thread_create_internal(thread_t *out, void (*start_routine)(void*), void *arg) {
    assert(out != NULL && start_routine != NULL);
    out->start_routine = start_routine;
    out->start_parameter = arg;
    THREAD_CHECK(pthread_create(&out->thread, NULL, thread_entry_wrapper, out));
}
