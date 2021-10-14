#include <stdarg.h>
#include <stdio.h>

#include <fsw/clock.h>
#include <fsw/debug.h>

void debugf(const char* format, ...) {
    va_list va;
    va_start(va, format);
    printf("[%3.9f] ", clock_timestamp() / 1000000000.0);
    vprintf(format, va);
    putchar('\n');
#ifndef __FREERTOS__
    // only need to flush on Linux
    fflush(stdout);
#endif
    va_end(va);
}
