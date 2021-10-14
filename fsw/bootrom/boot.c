#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <elf/elf.h>

extern uint8_t embedded_kernel[];

#define MEMORY_LOW (0x40000000)

static int debug_cb(const char* format, ...) {
    va_list va;
    va_start(va, format);
    printf("[BOOT ROM] ");
    int ret = vprintf(format, va);
    printf("\n");
    va_end(va);
    return ret;
}

static void no_load(uintptr_t vaddr, void *load_source, size_t filesz, size_t memsz) {
    (void) vaddr;
    (void) load_source;
    (void) filesz;
    (void) memsz;

    // do nothing
}

static void load_segment(uintptr_t vaddr, void *load_source, size_t filesz, size_t memsz) {
    void *load_target = (void *) vaddr;
    memcpy(load_target, load_source, filesz);
    memset(load_target + filesz, 0, memsz - filesz);
}

// first entrypoint from assembly; returns new stack relocation address.
uint32_t boot_phase_1(void) {
    printf("[BOOT ROM] Booting from ROM kernel\n");
    if (!elf_validate_header(embedded_kernel, debug_cb)) {
        printf("[BOOT ROM] Halting for repair\n");
        abort();
    }

    // scan segments to find a place to put our stack
    uint32_t stack_relocate_to = elf_scan_load_segments(embedded_kernel, debug_cb, MEMORY_LOW, no_load);
    if (stack_relocate_to == 0) {
        printf("[BOOT ROM] Halting for repair\n");
        abort();
    }

    return stack_relocate_to;
}

// second entrypoint from assembly; returns address of kernel entrypoint.
void *boot_phase_2(void) {
    // with our stack safely out of the way, we can now load the kernel
    uint32_t end_ptr = elf_scan_load_segments(embedded_kernel, debug_cb, MEMORY_LOW, load_segment);
    if (end_ptr == 0) {
        printf("[BOOT ROM] Halting for repair\n");
        abort();
    }

    // validate entrypoint
    Elf32_Ehdr *header = (Elf32_Ehdr*) embedded_kernel;
    if (header->e_entry < MEMORY_LOW || header->e_entry >= end_ptr) {
        printf("[BOOT ROM] Invalid entrypoint in kernel\n");
        printf("[BOOT ROM] Halting for repair\n");
        abort();
    }

    return (void *) header->e_entry;
}

#define SERIAL_BASE 0x09000000
#define SERIAL_FLAG_REGISTER 0x18
#define SERIAL_BUFFER_FULL (1 << 5)

void _putchar(char c) {
    /* Wait until the serial buffer is empty */
    while (*(volatile unsigned long*)(SERIAL_BASE + SERIAL_FLAG_REGISTER) & (SERIAL_BUFFER_FULL));
    /* Put our character, c, into the serial buffer */
    *(volatile unsigned long*)SERIAL_BASE = c;
}

void abort(void) {
    asm volatile("CPSID i");
    while (1) {
        asm volatile("WFI");
    }
}

// entrypoint on abort
void abort_report(void) {
    char error[] = "[BOOT ROM] ABORT\n";
    for (unsigned int i = 0; i < sizeof(error); i++) {
        _putchar(error[i]);
    }
    abort();
}
