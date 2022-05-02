#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>

#include <rtos/arm.h>
#include <rtos/gic.h>
#include <rtos/scheduler.h>
#include <rtos/scrubber.h>
#include <hal/debug.h>
#include <hal/init.h>

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
    schedule_first_clip();

    abortf("Scheduler halted.");
}
