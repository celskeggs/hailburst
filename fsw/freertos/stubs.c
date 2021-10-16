#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <FreeRTOS.h>
#include <task.h>

#include <rtos/arm.h>
#include <rtos/gic.h>
#include <rtos/scrubber.h>
#include <rtos/timer.h>
#include <hal/platform.h>

int errno = 0;

#define SERIAL_BASE 0x09000000
#define SERIAL_FLAG_REGISTER 0x18
#define SERIAL_BUFFER_FULL (1 << 5)

void _putchar(char c) {
    /* Wait until the serial buffer is empty */
    while (*(volatile unsigned long*)(SERIAL_BASE + SERIAL_FLAG_REGISTER) & (SERIAL_BUFFER_FULL));
    /* Put our character, c, into the serial buffer */
    *(volatile unsigned long*)SERIAL_BASE = c;
}

int putchar(int c) {
    _putchar(c);
    return 0;
}

__attribute__((noreturn)) void _Exit(int status) {
    printf("system exit status %d\n", status);
    abort();
}

void abort(void) {
    asm volatile("CPSID i");
    shutdown_gic();
    while (1) {
        asm volatile("WFI");
    }
}

void usleep(unsigned long usec) {
    TickType_t ticks = pdMS_TO_TICKS(usec / 1000);
    if (usec > 0 && ticks == 0) {
        ticks = 1;
    }
    vTaskDelay(ticks);
}

void *malloc(size_t size) {
    return pvPortMalloc(size);
}

void free(void *ptr) {
    vPortFree(ptr);
}

void perror(const char *s) {
    printf("perror: %s\n", s);
}

extern int main(int argc, char **argv, char **envp);

static void main_entrypoint(void *opaque) {
    (void) opaque;

    char *argv[] = {"kernel", NULL};
    char *envp[] = {NULL};
    exit(main(1, argv, envp));
}

void entrypoint(void *kernel_elf_rom) {
    configure_gic();

    // enable coprocessors for VFP
    arm_set_cpacr(arm_get_cpacr() | ARM_CPACR_CP10_FULL_ACCESS | ARM_CPACR_CP11_FULL_ACCESS);

    // enable VFP operations
    arm_set_fpexc(arm_get_fpexc() | ARM_FPEXC_EN);

    // enable scrubber
    scrubber_init(kernel_elf_rom);

    BaseType_t status = xTaskCreate(main_entrypoint, "main", 1000, NULL, PRIORITY_INIT, NULL);
    if (status != pdPASS) {
        printf("Error: could not create main task.\n");
        abort();
    }
    vTaskStartScheduler();
    printf("Scheduler halted.\n");
    abort();
}

void platform_init(void) {
    // nothing additional to do on FreeRTOS
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

void exception_report(uint32_t spsr, struct reg_state *state, unsigned int trap_mode) {
    uint64_t now = timer_now_ns();

    const char *trap_name = trap_mode < 3 ? trap_mode_names[trap_mode] : "???????";
    printf("%s\n", trap_name);
    TaskHandle_t failed_task = xTaskGetCurrentTaskHandle();
    const char *name = pcTaskGetName(failed_task);
    printf("%s occurred in task '%s' at PC=0x%08x SPSR=0x%08x\n", trap_name, name, state->lr, spsr);
    printf("Registers:  R0=0x%08x  R1=0x%08x  R2=0x%08x  R3=0x%08x\n", state->r0, state->r1, state->r2, state->r3);
    printf("Registers:  R4=0x%08x  R5=0x%08x  R6=0x%08x  R7=0x%08x\n", state->r4, state->r5, state->r6, state->r7);
    printf("Registers:  R8=0x%08x  R9=0x%08x R10=0x%08x R11=0x%08x\n", state->r8, state->r9, state->r10, state->r11);
    printf("Registers: R12=0x%08x\n", state->r12);
    printf("HALTING RTOS IN REACTION TO %s AT TIME=%" PRIu64 "\n", trap_name, now);
    // returns to an abort() call
}

// defined in entrypoint.s
extern volatile uint32_t trap_recursive_flag;

static volatile TaskHandle_t last_failed_task = NULL;

void task_abort_handler(unsigned int trap_mode) {
    const char *trap_name = trap_mode < 3 ? trap_mode_names[trap_mode] : "???????";
    printf("TASK %s\n", trap_name);
    TaskHandle_t failed_task = xTaskGetCurrentTaskHandle();
    assert(failed_task != NULL);
    const char *name = pcTaskGetName(failed_task);
    printf("%s occurred in task '%s'\n", trap_name, name);

    if (failed_task == xTaskGetIdleTaskHandle()) {
        // cannot suspend the IDLE task safely, because FreeRTOS requires that there always be an IDLE task
        printf("EXCEPTION OCCURRED IN IDLE TASK; HALTING RTOS.\n");
        abort();
    }

    if (last_failed_task == failed_task) {
        // should be different, because we shouldn't hit any aborts past this point
        printf("RECURSIVE ABORT; HALTING RTOS.\n");
        abort();
    }

    last_failed_task = failed_task;

    portMEMORY_BARRIER(); // so that we commit our changes to last_failed_task before updating the recursive flag

    assert(trap_recursive_flag == 1);
    trap_recursive_flag = 0;

    for (;;) {
        printf("SUSPENDING TASK.\n");
        // this will indeed suspend us in the middle of this abort handler... but that's fine! We don't actually need
        // to return all the way back to the interrupted task.
        vTaskSuspend(NULL);
        printf("Aborted task unexpectedly woke up!\n");
    }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *pcTaskName) {
    (void) task;

    uint64_t now = timer_now_ns();

    printf("STACK OVERFLOW occurred in task '%s'\n", pcTaskName);
    printf("HALTING IN REACTION TO STACK OVERFLOW AT TIME=%" PRIu64 "\n", now);
    abort();
}

void trace_task_switch(const char *task_name, unsigned int priority) {
    uint64_t now = timer_now_ns();
    printf("[%u.%09u] FreeRTOS scheduling %15s at priority %u\n",
           (uint32_t) (now / TIMER_NS_PER_SEC), (uint32_t) (now % TIMER_NS_PER_SEC),
           task_name, priority);
}
