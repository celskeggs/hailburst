#ifndef FSW_LINUX_FSW_DEBUG_H
#define FSW_LINUX_FSW_DEBUG_H

#include <assert.h>

#include <fsw/loglevel.h>

void debugf(loglevel_t level, const char *format, ...);

// generic but messier implementation
#define assertf(x, ...) assert((x) || (debugf(CRITICAL, "[assert] " __VA_ARGS__), 0))
#define abortf(...) (debugf(CRITICAL, "[assert]" __VA_ARGS__), abort())

#endif /* FSW_LINUX_FSW_DEBUG_H */
