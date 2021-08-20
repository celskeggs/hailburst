#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/elf32.h>

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
        __builtin_trap();
    }
}

int __llseek(int fd, unsigned long offset_high, unsigned long offset_low, off_t *result, int whence) {
    (void) fd;
    (void) offset_high;
    (void) offset_low;
    (void) result;
    (void) whence;
    __builtin_trap();
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

/*
void *mmap(void *addr, size_t length, int prot, int flags, int fd, long offset) {
    (void) addr;
    (void) length;
    (void) prot;
    (void) flags;
    (void) fd;
    (void) offset;
    __builtin_trap();
}

void *munmap(void *addr, size_t length) {
    (void) addr;
    (void) length;
    __builtin_trap();
}
*/

void _exit(int status) {
    (void) status;
    __builtin_trap();
}

extern __noreturn __libc_init(uintptr_t * elfdata, void (*onexit) (void));

static struct {
    uintptr_t argc;
    char *argv[2];
    char *envp[1];
    struct {
        unsigned long type;
        unsigned long v;
    } auxentries[2];
} fixed_elfdata = {
    .argc = 1,
    .argv = { "kernel", NULL },
    .envp = { NULL },
    .auxentries = {
        { AT_PAGESZ, 4096 },
        { AT_NULL },
    },
};

void entrypoint(void) {
    __libc_init((uintptr_t*) &fixed_elfdata, NULL);
}
