#include <fsw/debug.h>
#include <fsw/init.h>
#include <fsw/spacecraft.h>

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
            init->init_fn_1(init->init_param);
            count--;
        }
    }
    assertf(count == 0, "count=%u", count);
    debugf(DEBUG, "Completed all initpoints calls in stage %u.", stage);
}

void initialize_systems(void) {
    call_initpoints(STAGE_RAW);

    call_initpoints(STAGE_READY);

    // initialize spacecraft tasks
    debugf(INFO, "Preparing spacecraft for start...");
    spacecraft_init();
}
