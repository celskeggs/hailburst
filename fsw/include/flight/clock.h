#ifndef FSW_FLIGHT_CLOCK_H
#define FSW_FLIGHT_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

#include <hal/time.h>
#include <hal/timer.h>
#include <synch/config.h>

// use default number of replicas
#define CLOCK_REPLICAS CONFIG_APPLICATION_REPLICAS

extern int64_t clock_offset_adj_slow[CLOCK_REPLICAS];
extern int64_t clock_offset_adj_fast;

enum {
    // it's okay to use 0 here because, in the case that the actual correct calibration is +0, we will instead set it
    // to +1. Why is that okay? Because a 1ns error doesn't matter! (And this simplifies computing adjustments.)
    CLOCK_UNCALIBRATED = 0,
    CLOCK_REPLICA_MAJORITY = 1 + (CLOCK_REPLICAS / 2),
};

static inline int64_t clock_offset_adj_vote(void) {
    for (size_t result_i = 0; result_i <= CLOCK_REPLICAS - CLOCK_REPLICA_MAJORITY; result_i++) {
        int64_t result = clock_offset_adj_slow[result_i];
        size_t votes = 1;
        for (size_t compare_i = result_i + 1; compare_i < CLOCK_REPLICAS; compare_i++) {
            if (clock_offset_adj_slow[compare_i] == result) {
                votes++;
            }
        }
        if (votes >= CLOCK_REPLICA_MAJORITY) {
            return result;
        }
    }
    // fall back to less reliable calibration if no majority can be established.
    return clock_offset_adj_fast;
}

static inline mission_time_t clock_mission_adjust(local_time_t clock_mono) {
    return clock_mono + clock_offset_adj_vote();
}

static inline bool clock_is_calibrated(void) {
    return clock_offset_adj_vote() != CLOCK_UNCALIBRATED;
}

static inline mission_time_t clock_timestamp(void) {
    return clock_mission_adjust(timer_now_ns());
}

// unlike clock_mission_adjust and clock_timestamp, these do not do instant voting; this means that calibration errors
// may be correlated across modules and replicas, which is acceptable for debugging traces, but not for telemetry!
static inline mission_time_t clock_mission_adjust_fast(local_time_t clock_mono) {
    return clock_mono + clock_offset_adj_fast;
}

static inline mission_time_t clock_timestamp_fast(void) {
    return clock_mission_adjust_fast(timer_now_ns());
}

#endif /* FSW_FLIGHT_CLOCK_H */
