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
#include <fsw/init.h>
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

extern program_init initpoints_start[];
extern program_init initpoints_end[];

static void call_initpoints(void) {
    unsigned int num_initpoints = initpoints_end - initpoints_start;
    debugf(DEBUG, "Calling %u initpoints.", num_initpoints);
    for (unsigned int i = 0; i < num_initpoints; i++) {
        initpoints_start[i]();
    }
    debugf(DEBUG, "Completed all initpoints calls.");
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
    scrubber_set_kernel(kernel_elf_rom);

    call_initpoints();

#if ( configOVERRIDE_IDLE_TASK == 1 )
    // enable idle task
    thread_idle_init();
#endif

    // initialize spacecraft tasks
    debugf(INFO, "Preparing spacecraft for start...");
    spacecraft_init();

    debugf(WARNING, "Activating scheduler to bring spacecraft online.");
    vTaskStartScheduler();

    abortf("Scheduler halted.");
}

void trace_task_switch(const char *task_name, unsigned int priority) {
    debugf(TRACE, "FreeRTOS scheduling %15s at priority %u", task_name, priority);
}
