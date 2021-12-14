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
#include <fsw/debug.h>
#include <fsw/spacecraft.h>

void usleep(unsigned long usec) {
    vTaskDelay(timer_ns_to_ticks(usec * 1000));
}

void *malloc(size_t size) {
    return pvPortMalloc(size);
}

void free(void *ptr) {
    vPortFree(ptr);
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

    // initialize spacecraft tasks
    debugf(CRITICAL, "Preparing spacecraft for start...");
    spacecraft_init();

    debugf(CRITICAL, "Activating scheduler to bring spacecraft online.");
    vTaskStartScheduler();

    abortf("Scheduler halted.");
}

void trace_task_switch(const char *task_name, unsigned int priority) {
    debugf(TRACE, "FreeRTOS scheduling %15s at priority %u", task_name, priority);
}
