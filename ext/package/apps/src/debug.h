#ifndef APP_DEBUG_H
#define APP_DEBUG_H

#include "clock.h"

#ifdef __FREERTOS__

#include <io.h>

#define debug0(str)      (printk("[%3.9f] %s\n", clock_timestamp() / 1000000000.0, str))
#define debugf(fmt, ...) (printk("[%3.9f] " fmt "\n", clock_timestamp() / 1000000000.0, __VA_ARGS__))

#else

#include <stdio.h>

#define debug0(str)      (printf("[%3.9f] %s\n", clock_timestamp() / 1000000000.0, str), fflush(stdout))
#define debugf(fmt, ...) (printf("[%3.9f] " fmt "\n", clock_timestamp() / 1000000000.0, __VA_ARGS__), fflush(stdout))

#endif

#endif /* APP_DEBUG_H */
