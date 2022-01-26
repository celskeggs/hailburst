#include <inttypes.h>

#include <FreeRTOS.h>
#include <task.h>

#include <rtos/arm.h>
#include <rtos/crash.h>
#include <hal/atomic.h>
#include <hal/system.h>
#include <hal/thread.h>
#include <fsw/debug.h>
#include <fsw/init.h>

__attribute__((noreturn)) void task_suspend(void) {
    // this will indeed stop us in the middle of this abort handler... but that's fine! We don't actually need
    // to return all the way back to the interrupted task; this stack will simply be thrown away.
    debugf(DEBUG, "Suspending task.");
    // make sure interrupts are enabled before we use any WFI instructions
    asm volatile("CPSIE i" ::: "memory");
    for (;;) {
        taskYIELD();
    }
}

__attribute__((noreturn)) void restart_current_task(void) {
    thread_t current_thread = task_get_current();
    assert(current_thread != NULL);

    if (current_thread->restartable == RESTARTABLE) {
        // mark ourself as pending restart (handled by scheduler)
        current_thread->mut->hit_restart = true;
        current_thread->mut->needs_start = true;

        debugf(WARNING, "Suspending task to wait for restart.");
    } else {
        debugf(CRITICAL, "Cannot restart this task (not marked as RESTARTABLE); suspending instead.");
    }
    // wait forever for the restart task to run
    task_suspend();
}

struct reg_state {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r4;
    uint32_t r5;
    uint32_t r6;
    uint32_t r7;
    uint32_t r8;
    uint32_t r9;
    uint32_t r10;
    uint32_t r11;
    uint32_t r12;
    uint32_t lr;
};
static_assert(sizeof(struct reg_state) == 14 * 4, "invalid sizeof(struct reg_state)");

static const char *trap_mode_names[3] = {
    "UNDEFINED INSTRUCTION",
    "PREFETCH ABORT",
    "DATA ABORT",
};

extern volatile uint32_t ulPortInterruptNesting;

// defined in entrypoint.s
extern volatile uint32_t trap_recursive_flag;

void exception_report(uint32_t spsr, struct reg_state *state, unsigned int trap_mode) {
    uint64_t now = timer_now_ns();

    const char *trap_name = trap_mode < 3 ? trap_mode_names[trap_mode] : "???????";
    debugf(CRITICAL, "%s", trap_name);
    bool task_recursive = false;
    if (!scheduler_has_started()) {
        debugf(CRITICAL, "%s occurred before scheduler started", trap_name);
    } else {
        TaskHandle_t failed_task = task_get_current();
        debugf(CRITICAL, "%s occurred in task '%s'", trap_name, failed_task->pcTaskName);
        task_recursive = failed_task->mut->recursive_exception;
    }
    debugf(CRITICAL, "Status: PC=0x%08x SPSR=0x%08x",
           state->lr, spsr);
    debugf(CRITICAL, "Possible causes: InterruptNesting=%u GlobalRecurse=%u TaskRecurse=%u",
           ulPortInterruptNesting, trap_recursive_flag - 1, task_recursive);
    debugf(CRITICAL, "Registers:  R0=0x%08x  R1=0x%08x  R2=0x%08x  R3=0x%08x",
           state->r0, state->r1, state->r2, state->r3);
    debugf(CRITICAL, "Registers:  R4=0x%08x  R5=0x%08x  R6=0x%08x  R7=0x%08x",
           state->r4, state->r5, state->r6, state->r7);
    debugf(CRITICAL, "Registers:  R8=0x%08x  R9=0x%08x R10=0x%08x R11=0x%08x",
           state->r8, state->r9, state->r10, state->r11);
    debugf(CRITICAL, "Registers: R12=0x%08x", state->r12);

    debugf_stable(CRITICAL, StackEntry, "Traceback: 0x%08x", state->lr);
    debugf(CRITICAL, "HALTING RTOS IN REACTION TO %s AT TIME=%" PRIu64, trap_name, now);
    // returns to an abort() call
}

void task_abort_handler(unsigned int trap_mode) {
    const char *trap_name = trap_mode < 3 ? trap_mode_names[trap_mode] : "???????";
    debugf(WARNING, "TASK %s", trap_name);
    TaskHandle_t failed_task = task_get_current();
    debugf(WARNING, "%s occurred in task '%s'", trap_name, failed_task->pcTaskName);
    // must be false because we checked it just a moment ago in the trap handler
    assert(failed_task->mut->recursive_exception == false);

    // make sure we don't clear the global recursive flag until we've safely set the task recursive flag
    failed_task->mut->recursive_exception = true;
    assert(trap_recursive_flag == 1);
    atomic_store(trap_recursive_flag, 0);

    // this will indeed suspend us in the middle of this abort handler... but that's fine! We don't actually need
    // to return all the way back to the interrupted task.
    restart_current_task();
}
