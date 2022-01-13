#ifndef FSW_FREERTOS_FSW_CLOCK_H
#define FSW_FREERTOS_FSW_CLOCK_H

#include <stdint.h>

#include <rtos/timer_min.h>

static inline uint64_t clock_timestamp_monotonic(void) {
    return timer_now_ns();
}

// no difference between monotonic and adjusted clocks on FreeRTOS
static inline uint64_t clock_adjust_monotonic(uint64_t clock_mono) {
    return clock_mono;
}

static inline uint64_t clock_timestamp(void) {
    return timer_now_ns();
}

#endif /* FSW_FREERTOS_FSW_CLOCK_H */
