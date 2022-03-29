#ifndef FSW_HAL_TIME_H
#define FSW_HAL_TIME_H

#include <stdint.h>

enum {
    CLOCK_NS_PER_US  = 1000,
    CLOCK_NS_PER_MS  = 1000000,
    CLOCK_NS_PER_SEC = 1000000000,
};

// timestamp types
typedef uint64_t local_time_t;
typedef uint64_t mission_time_t;
typedef uint64_t duration_t;

#endif /* FSW_HAL_TIME_H */
