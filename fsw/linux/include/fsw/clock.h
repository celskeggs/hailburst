#ifndef FSW_LINUX_FSW_CLOCK_H
#define FSW_LINUX_FSW_CLOCK_H

#include <assert.h>
#include <stdint.h>
#include <time.h>

extern int64_t clock_offset_adj;

static inline uint64_t clock_timestamp_monotonic(void) {
    struct timespec ct;
    int time_ok = clock_gettime(CLOCK_MONOTONIC, &ct);
    assert(time_ok == 0);
    return 1000000000 * (int64_t) ct.tv_sec + (int64_t) ct.tv_nsec;
}

static inline uint64_t clock_adjust_monotonic(uint64_t clock_mono) {
    return clock_mono + clock_offset_adj;
}

static inline uint64_t clock_timestamp(void) {
    return clock_adjust_monotonic(clock_timestamp_monotonic());
}

#endif /* FSW_LINUX_FSW_CLOCK_H */
