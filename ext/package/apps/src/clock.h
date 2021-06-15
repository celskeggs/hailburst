#ifndef APP_CLOCK_H
#define APP_CLOCK_H

#include <assert.h>
#include <stdint.h>
#include <time.h>

#include "rmap.h"

extern int64_t clock_offset_adj;

void clock_init(rmap_monitor_t *mon, rmap_addr_t *address);

static inline uint64_t clock_timestamp(void) {
    struct timespec ct;
    int time_ok = clock_gettime(CLOCK_BOOTTIME, &ct);
    assert(time_ok == 0);
    return 1000000000 * (int64_t) ct.tv_sec + (int64_t) ct.tv_nsec + clock_offset_adj;
}

#endif /* APP_CLOCK_H */
