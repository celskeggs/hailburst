#include <hal/debug.h>
#include <hal/init.h>
#include <flight/spacecraft.h>

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
    unsigned int count2 = 0;
    for (program_init *init = initpoints_start; init < initpoints_end; init++) {
        if (init->init_stage == stage) {
            debugf(DEBUG, "Calling initpoint %u at %p.", count2, init->init_fn);
            init->init_fn(init->init_param);
            count2++;
        }
    }
    assertf(count == count2, "count=%u, count2=%u", count, count2);
    debugf(DEBUG, "Completed all initpoints calls in stage %u.", stage);
}

void initialize_systems(void) {
    call_initpoints(STAGE_RAW);
    call_initpoints(STAGE_READY);
    call_initpoints(STAGE_CRAFT);
}
