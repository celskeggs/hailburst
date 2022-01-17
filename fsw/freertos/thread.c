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
        uint64_t now = timer_now_ns();
        // give other tasks a chance to run
        taskYIELD();
        if (timer_now_ns() - now < (CLOCK_NS_PER_SEC / configTICK_RATE_HZ) / 10) {
            // that was fast... probably means we're the only task! so let's sleep for a while.

            // must have interrupts enabled for this to be safe
            debugf(TRACE, "Sending system to sleep in IDLE task.");
            assert((arm_get_cpsr() & ARM_CPSR_MASK_INTERRUPTS) == 0);
            asm volatile("WFI");
        } else {
            // someone else might have something to do... let's yield
        }
    }
}

TASK_REGISTER(idle_task, "IDLE", thread_idle_main, NULL, RESTARTABLE);

void task_entrypoint(TCB_t *state) {
    if (state->mut->hit_restart) {
        debugf(WARNING, "Pending restart on next scrubber cycle.");
        scrubber_cycle_wait();
    }

    task_clear_crash();

    state->start_routine(state->start_arg);

    restartf("Task main loop unexpectedly returned.");
}
