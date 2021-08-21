#ifndef APP_CLOCK_H
#define APP_CLOCK_H

#include <assert.h>
#include <stdint.h>
#include <time.h>
#include "rmap.h"

void clock_init(rmap_monitor_t *mon, rmap_addr_t *address);

#ifdef __FREERTOS__

#include <timer.h>

static inline uint64_t clock_timestamp_monotonic(void) {
    return timer_now_ns();
}

static inline uint64_t clock_timestamp(void) {
    return timer_now_ns();
}

#else

extern int64_t clock_offset_adj;

static inline uint64_t clock_timestamp_monotonic(void) {
    struct timespec ct;
    int time_ok = clock_gettime(CLOCK_BOOTTIME, &ct);
    assert(time_ok == 0);
    return 1000000000 * (int64_t) ct.tv_sec + (int64_t) ct.tv_nsec;
}

static inline uint64_t clock_timestamp(void) {
    return clock_timestamp_monotonic() + clock_offset_adj;
}

#endif

#endif /* APP_CLOCK_H */
