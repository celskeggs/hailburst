#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <bootrom/elf.h>

extern uint8_t embedded_kernel[];

#define MEMORY_LOW (0x40000000)

static bool validate_elf_header(uint8_t *kernel);
static uint32_t scan_load_segments(uint8_t *kernel, bool do_load);

// first entrypoint from assembly; returns new stack relocation address.
uint32_t boot_phase_1(void) {
    printf("[BOOT ROM] Booting from ROM kernel\n");
    if (!validate_elf_header(embedded_kernel)) {
        printf("[BOOT ROM] Halting for repair\n");
        abort();
    }

    // scan segments to find a place to put our stack
    uint32_t stack_relocate_to = scan_load_segments(embedded_kernel, false);
    if (stack_relocate_to == 0) {
        printf("[BOOT ROM] Halting for repair\n");
        abort();
    }

    return stack_relocate_to;
}

// second entrypoint from assembly; returns address of kernel entrypoint.
void *boot_phase_2(void) {
    // with our stack safely out of the way, we can now load the kernel
    uint32_t end_ptr = scan_load_segments(embedded_kernel, true);
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

static bool validate_elf_header(uint8_t *kernel) {
    Elf32_Ehdr *header = (Elf32_Ehdr*) kernel;

    if (header->e_ident_magic != ELF_MAGIC_NUMBER) {
        printf("[BOOT ROM] Invalid magic number 0x%08x\n", header->e_ident_magic);
        return false;
    }
    if (header->e_ident_class != ELF_EXPECTED_CLASS || header->e_ident_data != ELF_EXPECTED_DATA
            || header->e_ident_version != EV_CURRENT) {
        printf("[BOOT ROM] Invalid ELF identification block: class=%u, data=%u, version=%u\n",
               header->e_ident_class, header->e_ident_data, header->e_ident_version);
        return false;
    }
    if (header->e_type != ET_EXEC || header->e_machine != EM_ARM || header->e_version != EV_CURRENT) {
        printf("[BOOT ROM] Cannot execute ELF on ARM: type=%u, machine=%u, version=%u\n",
               header->e_type, header->e_machine, header->e_version);
        return false;
    }
    if (header->e_phoff == 0 || header->e_ehsize < sizeof(Elf32_Ehdr) || header->e_phnum == 0
            || header->e_phentsize < sizeof(Elf32_Phdr)) {
        printf("[BOOT ROM] Cannot read program headers: phoff=%u, ehsize=%u, phnum=%u, phentsize=%u\n",
               header->e_phoff, header->e_ehsize, header->e_phnum, header->e_phentsize);
        return false;
    }
    if ((header->e_flags & EF_ARM_EXPECT_MASK) != EF_ARM_EXPECTED) {
        printf("[BOOT ROM] Invalid ARM flags for boot: flags=0x%08x\n", header->e_flags);
        return false;
    }
    return true;
}

// returns pointer to free space after loaded segments.
static uint32_t scan_load_segments(uint8_t *kernel, bool do_load) {
    Elf32_Ehdr *header = (Elf32_Ehdr*) kernel;

    uint32_t next_load_address = MEMORY_LOW;

    for (size_t i = 0; i < header->e_phnum; i++) {
        Elf32_Phdr *segment = (Elf32_Phdr *) (kernel + header->e_phoff + header->e_phentsize * i);
        if (segment->p_type == PT_NULL || segment->p_type == PT_NOTE || segment->p_type == PT_PHDR
                || segment->p_type == PT_ARM_UNWIND) {
            // ignore these.
            continue;
        }
        if (segment->p_type != PT_LOAD) {
            printf("[BOOT ROM] Unrecognized elf segment [%u] type %u\n", i, segment->p_type);
            return 0;
        }
        // parse PT_LOAD segment
        if (segment->p_vaddr < next_load_address) {
            printf("[BOOT ROM] Invalid memory load vaddr: 0x%08x when last=0x%08x\n",
                   segment->p_vaddr, next_load_address);
            return 0;
        }
        if (segment->p_memsz < segment->p_filesz) {
            printf("[BOOT ROM] Invalid memsz 0x%08x < filesz 0x%08x\n", segment->p_memsz, segment->p_filesz);
            return 0;
        }
        void *load_target = (void *) segment->p_vaddr;
        void *load_source = (kernel + segment->p_offset);
        if (do_load) {
            memcpy(load_target, load_source, segment->p_filesz);
            memset(load_target + segment->p_filesz, 0, segment->p_memsz - segment->p_filesz);
        }
        next_load_address = segment->p_vaddr + segment->p_memsz;
    }

    return next_load_address;
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
