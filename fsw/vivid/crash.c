#include <inttypes.h>

#include <task.h>

#include <rtos/arm.h>
#include <hal/atomic.h>
#include <hal/debug.h>
#include <hal/init.h>
#include <hal/system.h>
#include <hal/thread.h>

// referenced in entrypoint.s
// force to 1 if we don't want to recover cleanly
#if ( VIVID_RECOVER_FROM_EXCEPTIONS == 1 )
uint32_t trap_recursive_flag = 0;
#else /* ( VIVID_RECOVER_FROM_EXCEPTIONS == 0 ) */
const uint32_t trap_recursive_flag = 1;
#endif

__attribute__((noreturn)) void restart_current_task(void) {
    if ((arm_get_cpsr() & ARM_CPSR_MASK_MODE) != ARM_SYS_MODE) {
        abortf("Restart condition hit in kernel context.");
    }

    thread_t current_thread = task_get_current();
    assert(current_thread != NULL);

    // mark clip
    current_thread->mut->hit_restart = true;
#if ( VIVID_RECOVERY_WAIT_FOR_SCRUBBER == 1 )
    scrubber_start_pend(&current_thread->mut->clip_pend);
#endif

    debugf(WARNING, "Suspending restarted task to wait for reschedule.");

    // make sure interrupts are enabled before we use any WFI instructions
    asm volatile("CPSIE i" ::: "memory");

    // wait forever for the reschedule
    task_yield();
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

static const char *trap_mode_names[] = {
    "UNDEFINED INSTRUCTION",
    "SUPERVISOR CALL ABORT",
    "PREFETCH ABORT",
    "DATA ABORT",
};

void exception_report(uint32_t spsr, struct reg_state *state, unsigned int trap_mode) {
    uint64_t now = timer_now_ns();

    const char *trap_name = trap_mode < 4 ? trap_mode_names[trap_mode] : "???????";
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
    debugf(CRITICAL, "Possible causes: InKernel=%u GlobalRecurse=%u TaskRecurse=%u",
           (spsr & ARM_CPSR_MASK_MODE) != ARM_SYS_MODE, atomic_load_relaxed(trap_recursive_flag) - 1, task_recursive);
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

__attribute__((noreturn)) void task_abort_handler(unsigned int trap_mode) {
    const char *trap_name = "???????";
    if (trap_mode < sizeof(trap_mode_names) / sizeof(trap_mode_names[0])) {
        trap_name = trap_mode_names[trap_mode];
    }
    debugf(WARNING, "TASK %s", trap_name);
    TaskHandle_t failed_task = task_get_current();
    debugf(WARNING, "%s occurred in task '%s'", trap_name, failed_task->pcTaskName);

#if ( VIVID_RECOVER_FROM_EXCEPTIONS == 0 )
    abortf("Recovery was disabled... shouldn't have reached task_abort_handler!");
#else /* ( VIVID_RECOVER_FROM_EXCEPTIONS == 1 ) */
    // must be false because we checked it just a moment ago in the trap handler
    assert(failed_task->mut->recursive_exception == false);

    // make sure we don't clear the global recursive flag until we've safely set the task recursive flag
    failed_task->mut->recursive_exception = true;
    assert(atomic_load_relaxed(trap_recursive_flag) == 1);
    atomic_store(trap_recursive_flag, 0);

    // this will indeed suspend us in the middle of this abort handler... but that's fine! We don't actually need
    // to return all the way back to the interrupted task.
    restart_current_task();
#endif
}
