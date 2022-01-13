#include <FreeRTOS.h>
#include <task.h>

#include <rtos/crash.h>
#include <rtos/scrubber.h>
#include <hal/atomic.h>
#include <hal/thread.h>
#include <fsw/debug.h>
#include <fsw/init.h>

extern void prvIdleTask(void *pvParameters);

TASK_REGISTER(idle_task, "IDLE", PRIORITY_IDLE, prvIdleTask, NULL, RESTARTABLE);

void task_entrypoint(TCB_t * state) {

    if (state->mut->hit_restart) {
        debugf(WARNING, "Pending restart on next scrubber cycle.");
        scrubber_cycle_wait(state == &idle_task);
    }

    task_clear_crash();

    state->start_routine(state->start_arg);

    restartf("Task main loop unexpectedly returned.");
}

extern void thread_start_internal(thread_t state);

void thread_restart_other_task(thread_t state) {
    assert(state != NULL);
    assert(state->restartable == RESTARTABLE);
    assert(state != xTaskGetCurrentTaskHandle());

    debugf(WARNING, "Restarting task '%s'", state->pcTaskName);

    // this needs to be in a critical section so that there is no period of time in which other tasks could run AND
    // the TaskHandle could refer to undefined memory.
    taskENTER_CRITICAL();
    vTaskDelete(state);
    state->mut->hit_restart = true;
    thread_start_internal(state);
    taskEXIT_CRITICAL();

    debugf(WARNING, "Completed restart for task '%s'", state->pcTaskName);
}

static void thread_registered_init(void) {
    debugf(DEBUG, "Starting %u pre-registered threads...", tasktable_end - tasktable_start);
    for (thread_t task = tasktable_start; task < tasktable_end; task++) {
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
