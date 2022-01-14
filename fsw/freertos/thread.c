#include <FreeRTOS.h>
#include <task.h>

#include <rtos/crash.h>
#include <rtos/scrubber.h>
#include <hal/atomic.h>
#include <hal/thread.h>
#include <fsw/debug.h>
#include <fsw/init.h>

static void thread_idle_main(void) {
    while (1) {
        // must have interrupts enabled for this to be safe
        assert((arm_get_cpsr() & ARM_CPSR_MASK_INTERRUPTS) == 0);
        asm volatile("WFI");
    }
}

TASK_REGISTER(idle_task, "IDLE", PRIORITY_IDLE, thread_idle_main, NULL, RESTARTABLE);

void task_entrypoint(TCB_t *state) {
    if (state->mut->hit_restart) {
        debugf(WARNING, "Pending restart on next scrubber cycle.");
        scrubber_cycle_wait();
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
