#include <stdio.h>

#include <FreeRTOS.h>

#include <hal/thread.h>

static void thread_entrypoint(void *opaque) {
    thread_t state = (thread_t) opaque;

    // discard return value
    (void) state->start_routine(state->arg);

    BaseType_t status;
    status = xSemaphoreGive(state->done);
    assert(status == pdTRUE);

    // suspend here so that the current task can be deleted
    while (1) {
        vTaskSuspend(NULL);
    }
}

void thread_create(thread_t *out, const char *name, unsigned int priority, void *(*start_routine)(void*), void *arg) {
    BaseType_t status;
    assert(out != NULL);

    assert(priority < configMAX_PRIORITIES);

    thread_t state = malloc(sizeof(*state));
    assert(state != NULL);

    state->start_routine = start_routine;
    state->arg = arg;
    state->done = xSemaphoreCreateBinary();
    assert(state->done != NULL);
    state->handle = NULL;

    if (name == NULL) {
        name = "anonymous_thread";
    }

    status = xTaskCreate(thread_entrypoint, name, 1000, state, priority, &state->handle);
    if (status == errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY) {
        printf("Out of memory while allocating anonymous task.\n");
    }
    assert(status == pdPASS);

    *out = state;
}

void thread_join(thread_t thread) {
    BaseType_t status;
    assert(thread != NULL && thread->done != NULL && thread->handle != NULL);

    status = xSemaphoreTake(thread->done, portMAX_DELAY);
    assert(status == pdTRUE);

    vSemaphoreDelete(thread->done);
    vTaskDelete(thread->handle);

    thread->done = NULL;
    thread->handle = NULL;
}

void thread_cancel(thread_t thread);
void thread_time_now(struct timespec *tp);
bool thread_join_timed(thread_t thread, const struct timespec *abstime); // true on success, false on timeout
void thread_disable_cancellation(void);
void thread_enable_cancellation(void);
void thread_testcancel(void);

void mutex_init(mutex_t *mutex) {
    assert(mutex != NULL);
    *mutex = xSemaphoreCreateMutex();
    assert(*mutex != NULL);
}

void mutex_destroy(mutex_t *mutex) {
    assert(mutex != NULL && *mutex != NULL);
    vSemaphoreDelete(*mutex);
    *mutex = NULL;
}

void cond_init(cond_t *cond) {
    assert(cond != NULL);
    mutex_init(&cond->state_mutex);
    cond->queue = NULL;
}

void cond_destroy(cond_t *cond) {
    assert(cond != NULL);
    mutex_destroy(&cond->state_mutex);
    assert(cond->queue == NULL);
}

void semaphore_init(semaphore_t *sema) {
    assert(sema != NULL);
    *sema = xSemaphoreCreateBinary();
    assert(*sema != NULL);
}

void semaphore_destroy(semaphore_t *sema) {
    assert(sema != NULL && *sema != NULL);
    vSemaphoreDelete(*sema);
    *sema = NULL;
}
