#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <elf/elf.h>

extern uint8_t embedded_kernel[];

// to avoid needing to include clock code here, but still be able to use debugf
int64_t clock_offset_adj = 0;

enum {
    MEMORY_LOW = 0x40000000,
};

static void no_load(uintptr_t vaddr, void *load_source, size_t filesz, size_t memsz, uint32_t flags) {
    (void) vaddr;
    (void) load_source;
    (void) filesz;
    (void) memsz;
    (void) flags;

    // do nothing
}

static void load_segment(uintptr_t vaddr, void *load_source, size_t filesz, size_t memsz, uint32_t flags) {
    (void) flags; // no distinction between permission types in main memory (flags are only needed by the scrubber)

    void *load_target = (void *) vaddr;
    memcpy(load_target, load_source, filesz);
    memset(load_target + filesz, 0, memsz - filesz);
}

// first entrypoint from assembly; returns new stack relocation address.
uint32_t boot_phase_1(void) {
    debugf_stable(CRITICAL, BootFromROMKernel, "[BOOT ROM] Booting from ROM kernel");
    if (!elf_validate_header(embedded_kernel)) {
        debugf(CRITICAL, "[BOOT ROM] Halting for repair");
        abort();
    }

    // scan segments to find a place to put our stack
    uint32_t stack_relocate_to = elf_scan_load_segments(embedded_kernel, MEMORY_LOW, no_load);
    if (stack_relocate_to == 0) {
        debugf(CRITICAL, "[BOOT ROM] Halting for repair");
        abort();
    }

    return stack_relocate_to;
}

// second entrypoint from assembly; returns address of kernel entrypoint.
void *boot_phase_2(void) {
    // with our stack safely out of the way, we can now load the kernel
    uint32_t end_ptr = elf_scan_load_segments(embedded_kernel, MEMORY_LOW, load_segment);
    if (end_ptr == 0) {
        debugf(CRITICAL, "[BOOT ROM] Halting for repair");
        abort();
    }

    // validate entrypoint
    Elf32_Ehdr *header = (Elf32_Ehdr*) embedded_kernel;
    if (header->e_entry < MEMORY_LOW || header->e_entry >= end_ptr) {
        debugf(CRITICAL, "[BOOT ROM] Invalid entrypoint in kernel");
        debugf(CRITICAL, "[BOOT ROM] Halting for repair");
        abort();
    }

    return (void *) header->e_entry;
}

#define SERIAL_BASE 0x09000000
#define SERIAL_FLAG_REGISTER 0x18
#define SERIAL_BUFFER_FULL (1 << 5)

void abort(void) {
    asm volatile("CPSID i");
    while (1) {
        asm volatile("WFI");
    }
}

// entrypoint on abort
void abort_report(void) {
    // TODO: figure out a better fallback here?
    abortf("[BOOT ROM] ABORT");
}
