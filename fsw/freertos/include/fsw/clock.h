#ifndef FSW_FREERTOS_FSW_CLOCK_H
#define FSW_FREERTOS_FSW_CLOCK_H

#include <stdint.h>

#include <rtos/timer_min.h>

static inline uint64_t clock_timestamp_monotonic(void) {
    return timer_now_ns();
}

static inline uint64_t clock_timestamp(void) {
    return timer_now_ns();
}

static inline void clock_wait_for_calibration(void) {
    /* do nothing; no calibration is required on FreeRTOS */
}

#endif /* FSW_FREERTOS_FSW_CLOCK_H */
