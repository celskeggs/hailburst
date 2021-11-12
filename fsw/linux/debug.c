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
    fflush(stdout);
    va_end(va);
}
