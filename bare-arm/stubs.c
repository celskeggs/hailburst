#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <FreeRTOS.h>
#include <task.h>
#include "gic.h"
#include "io.h"

int errno = 0;

#define SERIAL_BASE 0x09000000
#define SERIAL_FLAG_REGISTER 0x18
#define SERIAL_BUFFER_FULL (1 << 5)

void raw_putc(char c) {
    /* Wait until the serial buffer is empty */
    while (*(volatile unsigned long*)(SERIAL_BASE + SERIAL_FLAG_REGISTER) & (SERIAL_BUFFER_FULL));
    /* Put our character, c, into the serial buffer */
    *(volatile unsigned long*)SERIAL_BASE = c;
}

void printk(const char *format, ...) {
    va_list ap;
    size_t rv;
    char local_buffer[1024];

    va_start(ap, format);
    rv = vsnprintf(local_buffer, sizeof(local_buffer), format, ap);
    va_end(ap);

    if (rv > sizeof(local_buffer) - 1) {
        rv = sizeof(local_buffer) - 1;
    }
    for (size_t i = 0; i < rv; i++) {
        raw_putc(local_buffer[i]);
    }
}

__noreturn __assert_fail(const char *expr, const char *file, unsigned int line) {
    printk("ASSERT FAILED at %s:%u: %s\n", expr, file, line);
    abort();
}

void _exit(int status) {
    printk("system exit status %d\n", status);
    abort();
}

void abort(void) {
    asm volatile("CPSID i");
    shutdown_gic();
    while (1) {
        asm volatile("WFI");
    }
}

extern int main(int argc, char **argv, char **envp);

static void main_entrypoint(void *opaque) {
    (void) opaque;

    char *argv[] = {"kernel", NULL};
    char *envp[] = {NULL};
    _exit(main(1, argv, envp));
}

void entrypoint(void) {
    configure_gic();
    BaseType_t status = xTaskCreate(main_entrypoint, "main", 1000, NULL, 1, NULL);
    if (status != pdPASS) {
        printk("Error: could not create main task.\n");
        abort();
    }
    vTaskStartScheduler();
    printk("Scheduler halted.\n");
    abort();
}
