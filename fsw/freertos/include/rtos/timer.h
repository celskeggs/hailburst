#ifndef FSW_FREERTOS_RTOS_TIMER_H
#define FSW_FREERTOS_RTOS_TIMER_H

#include <stdint.h>

#include <FreeRTOS.h>
#include <projdefs.h>

#include <rtos/arm.h>

enum {
    TIMER_NS_PER_SEC = 1000000000,

    TIMER_ASSUMED_CNTFRQ = 62500000,

    TICK_PERIOD_NS = TIMER_NS_PER_SEC / configTICK_RATE_HZ,
    CLOCK_PERIOD_NS = TIMER_NS_PER_SEC / TIMER_ASSUMED_CNTFRQ,
    TICK_RATE_IN_CLOCK_UNITS = TICK_PERIOD_NS / CLOCK_PERIOD_NS,
};

static inline uint64_t timer_now_ns(void) {
    return arm_get_cntpct() * CLOCK_PERIOD_NS;
}

static inline TickType_t timer_ns_to_ticks(uint64_t nanoseconds) {
    TickType_t ticks = pdMS_TO_TICKS(nanoseconds / 1000000);
    if (ticks == 0 && nanoseconds > 0) {
        ticks = 1;
    } else if (ticks >= portMAX_DELAY) {
        ticks = portMAX_DELAY - 1;
    }
    return ticks;
}

static inline TickType_t timer_ticks_until_ns(uint64_t nanoseconds_abs) {
    uint64_t now = timer_now_ns();
    return now > nanoseconds_abs ? timer_ns_to_ticks(nanoseconds_abs - now) : 0;
}

#endif /* FSW_FREERTOS_RTOS_TIMER_H */
