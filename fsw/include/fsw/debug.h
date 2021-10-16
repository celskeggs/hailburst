#ifndef FSW_DEBUG_H
#define FSW_DEBUG_H

#include <assert.h>

void debugf(const char *format, ...);

#ifdef NDEBUG
#define assertf(x, ...) (void)0
#else
#define assertf(x, ...) ((void)((x) || (debugf("[assert] " __VA_ARGS__), __assert_fail(#x, __FILE__, __LINE__, __func__), 0)))
#endif

#endif /* FSW_DEBUG_H */
