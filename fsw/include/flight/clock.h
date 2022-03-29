#ifndef FSW_FLIGHT_CLOCK_H
#define FSW_FLIGHT_CLOCK_H

#include <hal/time.h>
#include <hal/timer.h>

extern int64_t clock_offset_adj;

static inline mission_time_t clock_mission_adjust(local_time_t clock_mono) {
    return clock_mono + clock_offset_adj;
}

static inline mission_time_t clock_timestamp(void) {
    return clock_mission_adjust(timer_now_ns());
}

#endif /* FSW_FLIGHT_CLOCK_H */
