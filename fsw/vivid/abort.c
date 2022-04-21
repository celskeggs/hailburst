#include <stdlib.h>

#include <rtos/gic.h>

void abort(void) {
    asm volatile("CPSID i");
    shutdown_gic();
    while (1) {
        asm volatile("WFI");
    }
}
