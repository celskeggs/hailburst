#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>

#include <FreeRTOS.h>
#include <task.h>

#include <rtos/arm.h>
#include <rtos/crash.h>
#include <rtos/gic.h>
#include <rtos/scrubber.h>
#include <rtos/timer.h>
#include <hal/platform.h>
#include <fsw/debug.h>

__attribute__((noreturn)) void _Exit(int status) {
    abortf("system exit status %d", status);
}

void usleep(unsigned long usec) {
    vTaskDelay(timer_ns_to_ticks(usec * 1000));
}

void *malloc(size_t size) {
    return pvPortMalloc(size);
}

void free(void *ptr) {
    vPortFree(ptr);
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

    // enable task restarting
    task_restart_init();

    // enable scrubber
    scrubber_init(kernel_elf_rom);

#if ( configOVERRIDE_IDLE_TASK == 1 )
    // enable idle task
    thread_idle_init();
#endif

    BaseType_t status = xTaskCreate(main_entrypoint, "main", 1000, NULL, PRIORITY_INIT, NULL);
    if (status != pdPASS) {
        abortf("Error: could not create main task.");
    }
    vTaskStartScheduler();
    abortf("Scheduler halted.");
}

void platform_init(void) {
    // nothing additional to do on FreeRTOS
}

void trace_task_switch(const char *task_name, unsigned int priority) {
    debugf(TRACE, "FreeRTOS scheduling %15s at priority %u", task_name, priority);
}
