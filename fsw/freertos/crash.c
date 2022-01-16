#include <inttypes.h>

#include <FreeRTOS.h>
#include <task.h>

#include <rtos/arm.h>
#include <rtos/crash.h>
#include <rtos/gic.h>
#include <hal/atomic.h>
#include <hal/thread.h>
#include <fsw/debug.h>
#include <fsw/init.h>

void abort(void) {
    asm volatile("CPSID i");
    shutdown_gic();
    while (1) {
        asm volatile("WFI");
    }
}

__attribute__((noreturn)) void task_suspend(void) {
    for (;;) {
        debugf(DEBUG, "Suspending task.");
        // this will indeed suspend us in the middle of this abort handler... but that's fine! We don't actually need
        // to return all the way back to the interrupted task.
        vTaskSuspend(NULL);
        debugf(CRITICAL, "Suspended task unexpectedly woke up!");
    }
}

static void restart_task_mainloop(void) {
    for (;;) {
        for (thread_t thread = tasktable_start; thread < tasktable_end; thread++) {
            if (thread->mut->needs_restart == true) {
                thread->mut->needs_restart = false;
                thread_restart_other_task(thread);
            }
        }
        task_doze();
    }
}

TASK_REGISTER(task_restart_task, "restart-task", restart_task_mainloop, NULL, NOT_RESTARTABLE);

__attribute__((noreturn)) void restart_current_task(void) {
    thread_t current_thread = task_get_current();
    assert(current_thread != NULL);

    if (current_thread->restartable == RESTARTABLE) {
        // mark ourself as pending restart
        current_thread->mut->needs_restart = true;
        // wake up the restart task
        task_rouse(&task_restart_task);
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

extern volatile uint32_t ulCriticalNesting;
extern volatile uint32_t ulPortInterruptNesting;

void exception_report(uint32_t spsr, struct reg_state *state, unsigned int trap_mode) {
    uint64_t now = timer_now_ns();

    const char *trap_name = trap_mode < 3 ? trap_mode_names[trap_mode] : "???????";
    debugf(CRITICAL, "%s", trap_name);
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        debugf(CRITICAL, "%s occurred before scheduler started", trap_name);
    } else {
        TaskHandle_t failed_task = xTaskGetCurrentTaskHandle();
        const char *name = pcTaskGetName(failed_task);
        debugf(CRITICAL, "%s occurred in task '%s'", trap_name, name);
    }
    debugf(CRITICAL, "Status: PC=0x%08x SPSR=0x%08x CriticalNesting=%u InterruptNesting=%u",
           state->lr, spsr, ulCriticalNesting, ulPortInterruptNesting);
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

// defined in entrypoint.s
extern volatile uint32_t trap_recursive_flag;

static volatile TaskHandle_t last_failed_task = NULL;

void task_clear_crash(void) {
    taskENTER_CRITICAL();
    if (last_failed_task == xTaskGetCurrentTaskHandle()) {
        last_failed_task = NULL;
    }
    taskEXIT_CRITICAL();
}

void task_abort_handler(unsigned int trap_mode) {
    const char *trap_name = trap_mode < 3 ? trap_mode_names[trap_mode] : "???????";
    debugf(WARNING, "TASK %s", trap_name);
    TaskHandle_t failed_task = xTaskGetCurrentTaskHandle();
    assert(failed_task != NULL);
    const char *name = pcTaskGetName(failed_task);
    debugf(WARNING, "%s occurred in task '%s'", trap_name, name);

    if (last_failed_task == failed_task) {
        // should be different, because we shouldn't hit any aborts past this point
        abortf("RECURSIVE ABORT; HALTING RTOS.");
    }

    last_failed_task = failed_task;

    portMEMORY_BARRIER(); // so that we commit our changes to last_failed_task before updating the recursive flag

    assert(trap_recursive_flag == 1);
    trap_recursive_flag = 0;

    // this will indeed suspend us in the middle of this abort handler... but that's fine! We don't actually need
    // to return all the way back to the interrupted task.
    restart_current_task();
}

void vApplicationStackOverflowHook(TaskHandle_t task, const char *pcTaskName) {
    (void) task;

    uint64_t now = timer_now_ns();

    debugf(CRITICAL, "STACK OVERFLOW occurred in task '%s'", pcTaskName);
    abortf("HALTING IN REACTION TO STACK OVERFLOW AT TIME=%" PRIu64, now);
}
