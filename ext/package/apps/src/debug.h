#ifndef APP_DEBUG_H
#define APP_DEBUG_H

#include <stdio.h>

#include "clock.h"

#define debug0(str) (printf("[%3.9f] %s\n", clock_timestamp() / 1000000000.0, str), fflush(stdout))
#define debugf(fmt, ...) (printf("[%3.9f] " fmt "\n", clock_timestamp() / 1000000000.0, __VA_ARGS__), fflush(stdout))

#endif /* APP_DEBUG_H */
