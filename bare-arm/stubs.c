#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <FreeRTOS.h>
#include <task.h>
#include "gic.h"

int errno = 0;

int isatty(int fd) {
    (void) fd;
    return 1;
}

#define SERIAL_BASE 0x09000000
#define SERIAL_FLAG_REGISTER 0x18
#define SERIAL_BUFFER_FULL (1 << 5)

void raw_putc(char c)
{
    /* Wait until the serial buffer is empty */
    while (*(volatile unsigned long*)(SERIAL_BASE + SERIAL_FLAG_REGISTER) & (SERIAL_BUFFER_FULL));
    /* Put our character, c, into the serial buffer */
    *(volatile unsigned long*)SERIAL_BASE = c;
}

ssize_t write(int fd, const void *buf, size_t size) {
    if (fd == 1 || fd == 2) {
        char *chbuf = (char *) buf;
        for (size_t i = 0; i < size; i++) {
            if (chbuf[i] == '\n') {
                raw_putc('\r');
            }
            raw_putc(chbuf[i]);
        }
        return size;
    } else {
        abort();
    }
}

int __llseek(int fd, unsigned long offset_high, unsigned long offset_low, off_t *result, int whence) {
    (void) fd;
    (void) offset_high;
    (void) offset_low;
    (void) result;
    (void) whence;
    abort();
}

static uint8_t static_heap[65536];
static uint8_t *static_heap_next = &static_heap[0];

void *malloc(size_t size) {
    if (size > sizeof(static_heap) || static_heap_next + size > static_heap + sizeof(static_heap)) {
        return NULL;
    }
    void *out = static_heap_next;
    static_heap_next += size;
    return out;
}

void free(void *addr) {
    (void) addr;
}

void _exit(int status) {
    printf("EXIT called with status=%d\n", status);
    abort();
}

void abort(void) {
    asm volatile("CPSID i");
    shutdown_gic();
    while (1) {
        asm volatile("WFI");
    }
}

extern void __libc_init_stdio(void);
extern int main(int argc, char **argv, char **envp);

static void main_entrypoint(void *opaque) {
    (void) opaque;

    char *argv[] = {"kernel", NULL};
    char *envp[] = {NULL};
    exit(main(1, argv, envp));
}

void entrypoint(void) {
    // we don't call __libc_init because we don't actually NEED all of that machinery
    __libc_init_stdio();
    configure_gic();
    BaseType_t status = xTaskCreate(main_entrypoint, "main", 200, NULL, 1, NULL);
    if (status != pdPASS) {
        puts("Error: could not create main task.");
        abort();
    }
    vTaskStartScheduler();
    printf("Scheduler halted.\n");
    abort();
}
