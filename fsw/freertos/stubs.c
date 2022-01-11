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

static void call_initpoints(enum init_stage stage) {
    unsigned int count = 0;
    for (program_init *init = initpoints_start; init < initpoints_end; init++) {
        if (init->init_stage == stage) {
            count++;
        }
    }
    debugf(DEBUG, "Calling %u initpoints in stage %u.", count, stage);
    for (program_init *init = initpoints_start; init < initpoints_end; init++) {
        if (init->init_stage == stage) {
            init->init_fn();
            count--;
        }
    }
    assertf(count == 0, "count=%u", count);
    debugf(DEBUG, "Completed all initpoints calls in stage %u.", stage);
}

static void configure_floating_point(void) {
    // enable coprocessors for VFP
    arm_set_cpacr(arm_get_cpacr() | ARM_CPACR_CP10_FULL_ACCESS | ARM_CPACR_CP11_FULL_ACCESS);

    // enable VFP operations
    arm_set_fpexc(arm_get_fpexc() | ARM_FPEXC_EN);
}
PROGRAM_INIT(STAGE_RAW, configure_floating_point);

void entrypoint(void *kernel_elf_rom) {
    // enable scrubber
    scrubber_set_kernel(kernel_elf_rom);

    call_initpoints(STAGE_RAW);

    call_initpoints(STAGE_READY);

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
