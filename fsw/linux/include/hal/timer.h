#ifndef FSW_LINUX_HAL_TIMER_H
#define FSW_LINUX_HAL_TIMER_H

#include <time.h>

#include <hal/time.h>

static inline local_time_t timer_now_ns(void) {
    struct timespec ct;
    int time_ok = clock_gettime(CLOCK_MONOTONIC, &ct);
    assert(time_ok == 0);
    return CLOCK_NS_PER_SEC * (int64_t) ct.tv_sec + (int64_t) ct.tv_nsec;
}

#endif /* FSW_LINUX_HAL_TIMER_H */
