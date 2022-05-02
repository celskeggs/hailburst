#ifndef FSW_LINUX_FSW_DEBUG_H
#define FSW_LINUX_FSW_DEBUG_H

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <hal/loglevel.h>
#include <flight/clock.h>

#define TIMEFMT "%3.9f"
#define TIMEARG(x) ((x) / 1000000000.0)

/* note: should not use TIMEARG like this in general; the argument must not be a function call on Vivid */
#define debugf(level, fmt, ...)                                                                                       \
        ({ printf("[" TIMEFMT "] " fmt "\n", TIMEARG(clock_timestamp_fast()), ## __VA_ARGS__); fflush(stdout); })
#define debugf_stable(level, stable_id, fmt, ...) debugf(level, fmt, ## __VA_ARGS)

// generic but messier implementation
#define assertf(x, ...) assert((x) || (debugf(CRITICAL, "[assert] " __VA_ARGS__), 0))
#define abortf(...) (debugf(CRITICAL, "[assert] " __VA_ARGS__), abort())

#endif /* FSW_LINUX_FSW_DEBUG_H */
