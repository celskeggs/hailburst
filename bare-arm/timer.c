#include <stdio.h>
#include <inttypes.h>
#include <FreeRTOS.h>
#include "arm.h"
#include "gic.h"
#include <timer.h>

extern void FreeRTOS_Tick_Handler(void);

enum {
    IRQ_PHYS_TIMER = IRQ_PPI_BASE + 14,
};

static void timer_callback(void *opaque) {
    (void) opaque;
    // update the next callback time to the next timing tick
    uint64_t new_time = arm_get_cntp_cval() + TICK_RATE_IN_CLOCK_UNITS;
    arm_set_cntp_cval(new_time);
    printf("Tick hit at %" PRIu64 "; scheduled next tick for %" PRIu64 "\n", timer_now_ns(), new_time * CLOCK_PERIOD_NS);
    // call tick handler
    FreeRTOS_Tick_Handler();
}

void vConfigureTickInterrupt(void) {
    assert(TIMER_ASSUMED_CNTFRQ == arm_get_cntfrq());

    uint64_t start_time = arm_get_cntpct();
    // align start time with tick rate
    start_time -= start_time % TICK_RATE_IN_CLOCK_UNITS;
    // advance forward to one period in the future
    start_time += TICK_RATE_IN_CLOCK_UNITS;

    arm_set_cntp_cval(start_time);

    // set the enable bit and don't set the mask bit
    arm_set_cntp_ctl(ARM_TIMER_ENABLE);

    // enable the IRQ
    enable_irq(IRQ_PHYS_TIMER, timer_callback, NULL);
}
