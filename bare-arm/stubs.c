#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <FreeRTOS.h>
#include <task.h>
#include <arm.h>
#include <gic.h>

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

void entrypoint(void) {
    configure_gic();

    // enable coprocessors for VFP
    arm_set_cpacr(arm_get_cpacr() | ARM_CPACR_CP10_FULL_ACCESS | ARM_CPACR_CP11_FULL_ACCESS);

    // enable VFP operations
    arm_set_fpexc(arm_get_fpexc() | ARM_FPEXC_EN);

    BaseType_t status = xTaskCreate(main_entrypoint, "main", 1000, NULL, PRIORITY_INIT, NULL);
    if (status != pdPASS) {
        printf("Error: could not create main task.\n");
        abort();
    }
    vTaskStartScheduler();
    printf("Scheduler halted.\n");
    abort();
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
    uint32_t r14;
    uint32_t lr;
    uint32_t spsr;
};
_Static_assert(sizeof(struct reg_state) == 16 * 4, "invalid sizeof(struct reg_state)");

void data_abort_report(struct reg_state *state) {
    printf("DATA ABORT\n");
    TaskHandle_t failed_task = xTaskGetCurrentTaskHandle();
    const char *name = pcTaskGetName(failed_task);
    printf("Data abort occurred in task '%s' at PC=0x%08x SP=0x%08x SPSR=0x%08x\n", name, state->lr, (uint32_t) (state + 1), state->spsr);
    printf("Registers:  R0=0x%08x  R1=0x%08x  R2=0x%08x  R3=0x%08x\n", state->r0, state->r1, state->r2, state->r3);
    printf("Registers:  R4=0x%08x  R5=0x%08x  R6=0x%08x  R7=0x%08x\n", state->r4, state->r5, state->r6, state->r7);
    printf("Registers:  R8=0x%08x  R9=0x%08x R10=0x%08x R11=0x%08x\n", state->r8, state->r9, state->r10, state->r11);
    printf("Registers: R12=0x%08x R14=0x%08x\n", state->r12, state->r14);
    printf("HALTING IN REACTION TO DATA ABORT\n");
    abort();
}
