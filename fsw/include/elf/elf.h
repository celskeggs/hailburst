#ifndef FSW_ELF_ELF_H
#define FSW_ELF_ELF_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <fsw/debug.h>

typedef uint32_t Elf32_Addr;
typedef uint16_t Elf32_Half;
typedef uint32_t Elf32_Off;
typedef int32_t  Elf32_Sword;
typedef uint32_t Elf32_Word;

enum {
    ELF_MAGIC_NUMBER = (0x7F << 0) | ('E' << 8) | ('L' << 16) | ('F' << 24), /* little-endian magic number */
    ELF_EXPECTED_CLASS = 1, /* 32-bit object */
    ELF_EXPECTED_DATA  = 1, /* little-endian */
    EV_CURRENT = 1, /* current ELF version */
    ET_EXEC = 2, /* executable file */
    EM_ARM = 40, /* ARM/Thumb Architecture */

    EF_ARM_BE8           = 0x00800000,
    EF_ARM_EABIMASK      = 0xFF000000,
    EF_ARM_EABIVERSION   = 0x05000000,

    EF_ARM_EXPECT_MASK = EF_ARM_BE8 | EF_ARM_EABIMASK, /* don't care about SYMSARESORTED */
    EF_ARM_EXPECTED = EF_ARM_EABIVERSION,
};

typedef struct {
    uint32_t       e_ident_magic;
    uint8_t        e_ident_class;
    uint8_t        e_ident_data;
    uint8_t        e_ident_version;
    unsigned char  e_ident[9];
    Elf32_Half     e_type;
    Elf32_Half     e_machine;
    Elf32_Word     e_version;
    Elf32_Addr     e_entry;
    Elf32_Off      e_phoff;
    Elf32_Off      e_shoff;
    Elf32_Word     e_flags;
    Elf32_Half     e_ehsize;
    Elf32_Half     e_phentsize;
    Elf32_Half     e_phnum;
    Elf32_Half     e_shentsize;
    Elf32_Half     e_shnum;
    Elf32_Half     e_shstrndx;
} Elf32_Ehdr;
static_assert(sizeof(Elf32_Ehdr) == 52, "invalid sizeof(Elf32_Ehdr)");

typedef struct {
    Elf32_Word    p_type;
    Elf32_Off     p_offset;
    Elf32_Addr    p_vaddr;
    Elf32_Addr    p_paddr;
    Elf32_Word    p_filesz;
    Elf32_Word    p_memsz;
    Elf32_Word    p_flags;
    Elf32_Word    p_align;
} Elf32_Phdr;
static_assert(sizeof(Elf32_Phdr) == 32, "invalid sizeof(Elf32_Phdr)");

// enum values for program header types
enum {
    PT_NULL = 0,
    PT_LOAD = 1,
    PT_DYNAMIC = 2,
    PT_INTERP = 3,
    PT_NOTE = 4,
    PT_SHLIB = 5,
    PT_PHDR = 6,

    PT_ARM_UNWIND = 0x70000001,
};

enum {
    PF_X = 0x1, /* execute */
    PF_W = 0x2, /* write */
    PF_R = 0x4, /* read */
};

typedef void (*elf_scan_cb_t)(uintptr_t vaddr, void *load_source, size_t filesz, size_t memsz, uint32_t flags);

bool elf_validate_header(uint8_t *kernel);
uint32_t elf_scan_load_segments(uint8_t *kernel, uint32_t lowest_address, elf_scan_cb_t visitor);

#endif /* FSW_ELF_ELF_H */
