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

    // call all initpoints and spacecraft_init()
    initialize_systems();

    debugf(WARNING, "Activating scheduler to bring spacecraft online.");
    vTaskStartScheduler();

    abortf("Scheduler halted.");
}

void trace_task_switch(const char *task_name) {
    debugf(TRACE, "FreeRTOS scheduling %15s", task_name);
}
