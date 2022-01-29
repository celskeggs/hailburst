#include <FreeRTOS.h>
#include <task.h>

#include <rtos/crash.h>
#include <rtos/scrubber.h>
#include <hal/atomic.h>
#include <hal/debug.h>
#include <hal/init.h>
#include <hal/thread.h>

void task_entrypoint(TCB_t *state) {
    if (state->mut->hit_restart) {
        debugf(WARNING, "Pending restart on next scrubber cycle.");
        scrubber_cycle_wait();
    }

    /* clear crash flag */
    state->mut->recursive_exception = false;

    state->start_routine(state->start_arg);

    restartf("Task main loop unexpectedly returned.");
}
