#include <hal/thread.h>
#include <fsw/debug.h>

static void *thread_entry_wrapper(void *param) {
    assert(param != NULL);
    thread_t thread = (thread_t) param;
    assert(thread->start_routine != NULL);
    thread->start_routine(thread->start_parameter);
    return NULL;
}

void thread_start_internal(thread_t thread) {
    THREAD_CHECK(pthread_create(&thread->thread, NULL, thread_entry_wrapper, thread));
}

void thread_create_internal(thread_t *out, void (*start_routine)(void*), void *arg) {
    assert(out != NULL && start_routine != NULL);
    thread_t state = malloc(sizeof(struct thread_st));
    assert(state != NULL);
    state->start_routine = start_routine;
    state->start_parameter = arg;
    thread_start_internal(state);
    *out = state;
}
