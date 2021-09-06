#ifndef FSW_DEBUG_H
#define FSW_DEBUG_H

#include <stdio.h>

#include <fsw/clock.h>

#ifdef __FREERTOS__
#define _AND_FLUSH
#else
#define _AND_FLUSH , fflush(stdout)
#endif

#define debug0(str)      (printf("[%3.9f] %s\n", clock_timestamp() / 1000000000.0, str) _AND_FLUSH)
#define debugf(fmt, ...) (printf("[%3.9f] " fmt "\n", clock_timestamp() / 1000000000.0, __VA_ARGS__) _AND_FLUSH)

#endif /* FSW_DEBUG_H */
