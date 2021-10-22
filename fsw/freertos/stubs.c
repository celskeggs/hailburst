#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <FreeRTOS.h>
#include <task.h>

#include <rtos/arm.h>
#include <rtos/crash.h>
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

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize) {
    *ppxIdleTaskTCBBuffer = malloc(sizeof(StaticTask_t));
    assert(*ppxIdleTaskTCBBuffer != NULL);
    *ppxIdleTaskStackBuffer = malloc(sizeof(StackType_t) * configMINIMAL_STACK_SIZE);
    assert(*ppxIdleTaskStackBuffer != NULL);
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

void entrypoint(void *kernel_elf_rom) {
    configure_gic();

    // enable coprocessors for VFP
    arm_set_cpacr(arm_get_cpacr() | ARM_CPACR_CP10_FULL_ACCESS | ARM_CPACR_CP11_FULL_ACCESS);

    // enable VFP operations
    arm_set_fpexc(arm_get_fpexc() | ARM_FPEXC_EN);

    // enable task restarting
    task_restart_init();

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

void trace_task_switch(const char *task_name, unsigned int priority) {
    uint64_t now = timer_now_ns();
    printf("[%u.%09u] FreeRTOS scheduling %15s at priority %u\n",
           (uint32_t) (now / TIMER_NS_PER_SEC), (uint32_t) (now % TIMER_NS_PER_SEC),
           task_name, priority);
}
