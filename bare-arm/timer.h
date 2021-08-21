#ifndef BARE_ARM_TIMER_H
#define BARE_ARM_TIMER_H

#include <stdint.h>
#include "arm.h"

enum {
    TIMER_NS_PER_SEC = 1000000000,

    TIMER_ASSUMED_CNTFRQ = 62500000,

    TICK_PERIOD_NS = TIMER_NS_PER_SEC / configTICK_RATE_HZ,
    CLOCK_PERIOD_NS = TIMER_NS_PER_SEC / TIMER_ASSUMED_CNTFRQ,
    TICK_RATE_IN_CLOCK_UNITS = TICK_PERIOD_NS / CLOCK_PERIOD_NS,
};

static inline uint64_t timer_now_ns(void) {
    // TODO: I may want to consider hardcoding or caching the scale value...
    return arm_get_cntpct() * CLOCK_PERIOD_NS;
}

#endif /* BARE_ARM_TIMER_H */