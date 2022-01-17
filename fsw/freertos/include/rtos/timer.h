#ifndef FSW_FREERTOS_RTOS_TIMER_H
#define FSW_FREERTOS_RTOS_TIMER_H

#include <stdint.h>

#include <FreeRTOS.h>
#include <projdefs.h>

#include <rtos/arm.h>
#include <rtos/timer_min.h>

enum {
    TICK_PERIOD_NS = CLOCK_NS_PER_SEC / configTICK_RATE_HZ,
    TICK_RATE_IN_CLOCK_UNITS = TICK_PERIOD_NS / CLOCK_PERIOD_NS,

    TICK_NS_DIVISOR = CLOCK_NS_PER_SEC / configTICK_RATE_HZ,
};
static_assert(TICK_NS_DIVISOR * configTICK_RATE_HZ == CLOCK_NS_PER_SEC, "lossless calculation check");

static inline TickType_t timer_ticks_until_ns(uint64_t nanoseconds_abs) {
    uint64_t now = timer_now_ns();
    if (now >= nanoseconds_abs) {
        return 0;
    }
    TickType_t now_ticks = now / TICK_NS_DIVISOR;
    TickType_t abs_ticks = (nanoseconds_abs - 1) / TICK_NS_DIVISOR + 1;
    assert(now_ticks < abs_ticks);
    TickType_t delay = abs_ticks - now_ticks;
    if (delay >= portMAX_DELAY) {
        delay = portMAX_DELAY - 1;
    }
    return delay;
}

#endif /* FSW_FREERTOS_RTOS_TIMER_H */
