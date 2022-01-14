#ifndef FSW_FREERTOS_RTOS_TIMER_MIN_H
#define FSW_FREERTOS_RTOS_TIMER_MIN_H

#include <stdint.h>

#include <rtos/arm.h>

enum {
    CLOCK_NS_PER_SEC = 1000000000,
    TIMER_ASSUMED_CNTFRQ = 62500000,

    CLOCK_PERIOD_NS = CLOCK_NS_PER_SEC / TIMER_ASSUMED_CNTFRQ,
};

static inline uint64_t timer_now_ns(void) {
    return arm_get_cntpct() * CLOCK_PERIOD_NS;
}

#endif /* FSW_FREERTOS_RTOS_TIMER_MIN_H */
