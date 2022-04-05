#ifndef FSW_VIVID_HAL_TIMER_H
#define FSW_VIVID_HAL_TIMER_H

#include <rtos/arm.h>
#include <hal/time.h>

enum {
    TIMER_ASSUMED_CNTFRQ = 62500000,

    CLOCK_PERIOD_NS = CLOCK_NS_PER_SEC / TIMER_ASSUMED_CNTFRQ,
};

static inline local_time_t timer_now_ns(void) {
    return arm_get_cntpct() * CLOCK_PERIOD_NS;
}

#endif /* FSW_VIVID_HAL_TIMER_H */
