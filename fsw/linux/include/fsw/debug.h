#ifndef FSW_LINUX_FSW_DEBUG_H
#define FSW_LINUX_FSW_DEBUG_H

#include <assert.h>

void debugf(const char *format, ...);

// generic but messier implementation
#define assertf(x, ...) assert((x) || (debugf("[assert] " __VA_ARGS__), 0))

#endif /* FSW_LINUX_FSW_DEBUG_H */
