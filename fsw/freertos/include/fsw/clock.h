#ifndef FSW_FREERTOS_FSW_CLOCK_H
#define FSW_FREERTOS_FSW_CLOCK_H

#include <stdint.h>

#include <rtos/timer.h>
#include <fsw/fakewire/rmap.h>

void clock_init(rmap_monitor_t *mon, rmap_addr_t *address);

static inline uint64_t clock_timestamp_monotonic(void) {
    return timer_now_ns();
}

static inline uint64_t clock_timestamp(void) {
    return timer_now_ns();
}

#endif /* FSW_FREERTOS_FSW_CLOCK_H */
