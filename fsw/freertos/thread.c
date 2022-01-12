#include <FreeRTOS.h>
#include <task.h>

#include <rtos/crash.h>
#include <rtos/scrubber.h>
#include <hal/atomic.h>
#include <hal/thread.h>
#include <fsw/debug.h>
#include <fsw/init.h>

#if ( configOVERRIDE_IDLE_TASK == 1 )
extern void prvIdleTask(void *pvParameters);

TASK_REGISTER(idle_task, "IDLE", PRIORITY_IDLE, prvIdleTask, NULL, RESTARTABLE);
#else
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize) {

    *ppxIdleTaskTCBBuffer = malloc(sizeof(StaticTask_t));
    assert(*ppxIdleTaskTCBBuffer != NULL);
    *ppxIdleTaskStackBuffer = malloc(sizeof(StackType_t) * configMINIMAL_STACK_SIZE);
    assert(*ppxIdleTaskStackBuffer != NULL);
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}
#endif

thread_t iter_first_thread = NULL;

static void thread_entrypoint(void *opaque) {
    thread_t state = (thread_t) opaque;

    if (state->hit_restart) {
        debugf(WARNING, "Pending restart on next scrubber cycle.");
#if ( configOVERRIDE_IDLE_TASK == 1 )
        scrubber_cycle_wait(state == &idle_task);
#else
        scrubber_cycle_wait(false);
#endif
    }

    task_clear_crash();

    state->start_routine(state->arg);

    restartf("Task main loop unexpectedly returned.");
}

static void thread_start_internal(thread_t state) {
    state->handle = xTaskCreateStatic(thread_entrypoint, state->name, STACK_SIZE, state, state->priority,
                                      state->preallocated_stack, &state->preallocated_task_memory);
    assert(state->handle != NULL);
    // just a check for implementation assumptions regarding task creation
    assert((void*) state->handle == (void*) &state->preallocated_task_memory);

    vTaskSetApplicationTaskTag(state->handle, (void *) state);
}

void thread_restart_other_task(thread_t state) {
    assert(state != NULL && state->handle != NULL);
    assert(state->restartable == RESTARTABLE);
    assert(state->handle != xTaskGetCurrentTaskHandle());

    debugf(WARNING, "Restarting task '%s'", state->name);

    // this needs to be in a critical section so that there is no period of time in which other tasks could run AND
    // the TaskHandle could refer to undefined memory.
    taskENTER_CRITICAL();
    vTaskDelete(state->handle);
    state->hit_restart = true;
    thread_start_internal(state);
    taskEXIT_CRITICAL();

    debugf(WARNING, "Completed restart for task '%s'", state->name);
}

extern struct thread_st tasktable_start[];
extern struct thread_st tasktable_end[];

static void thread_registered_init(void) {
    debugf(DEBUG, "Starting %u pre-registered threads...", tasktable_end - tasktable_start);
    for (thread_t task = tasktable_start; task < tasktable_end; task++) {
        task->iter_next_thread = iter_first_thread;
        atomic_store(iter_first_thread, task);
        thread_start_internal(task);
    }
    debugf(DEBUG, "Pre-registered threads started!");
}

PROGRAM_INIT(STAGE_READY, thread_registered_init);

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
