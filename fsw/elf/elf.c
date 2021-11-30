#include <elf/elf.h>

bool elf_validate_header(uint8_t *kernel) {
    Elf32_Ehdr *header = (Elf32_Ehdr*) kernel;

    if (header->e_ident_magic != ELF_MAGIC_NUMBER) {
        debugf(CRITICAL, "Invalid magic number 0x%08x", header->e_ident_magic);
        return false;
    }
    if (header->e_ident_class != ELF_EXPECTED_CLASS || header->e_ident_data != ELF_EXPECTED_DATA
            || header->e_ident_version != EV_CURRENT) {
        debugf(CRITICAL, "Invalid ELF identification block: class=%u, data=%u, version=%u",
               header->e_ident_class, header->e_ident_data, header->e_ident_version);
        return false;
    }
    if (header->e_type != ET_EXEC || header->e_machine != EM_ARM || header->e_version != EV_CURRENT) {
        debugf(CRITICAL, "Cannot execute ELF on ARM: type=%u, machine=%u, version=%u",
               header->e_type, header->e_machine, header->e_version);
        return false;
    }
    if (header->e_phoff == 0 || header->e_ehsize < sizeof(Elf32_Ehdr) || header->e_phnum == 0
            || header->e_phentsize < sizeof(Elf32_Phdr)) {
        debugf(CRITICAL, "Cannot read program headers: phoff=%u, ehsize=%u, phnum=%u, phentsize=%u",
               header->e_phoff, header->e_ehsize, header->e_phnum, header->e_phentsize);
        return false;
    }
    if ((header->e_flags & EF_ARM_EXPECT_MASK) != EF_ARM_EXPECTED) {
        debugf(CRITICAL, "Invalid ARM flags for boot: flags=0x%08x", header->e_flags);
        return false;
    }
    return true;
}

// returns pointer to free space after loaded segments.
uint32_t elf_scan_load_segments(uint8_t *kernel, uint32_t lowest_address, elf_scan_cb_t visitor) {
    Elf32_Ehdr *header = (Elf32_Ehdr*) kernel;

    uint32_t next_load_address = lowest_address;

    for (size_t i = 0; i < header->e_phnum; i++) {
        Elf32_Phdr *segment = (Elf32_Phdr *) (kernel + header->e_phoff + header->e_phentsize * i);
        if (segment->p_type == PT_NULL || segment->p_type == PT_NOTE || segment->p_type == PT_PHDR
                || segment->p_type == PT_ARM_UNWIND) {
            // ignore these.
            continue;
        }
        if (segment->p_type != PT_LOAD) {
            debugf(CRITICAL, "Unrecognized elf segment [%u] type %u", i, segment->p_type);
            return 0;
        }
        // parse PT_LOAD segment
        if (segment->p_vaddr < next_load_address) {
            debugf(CRITICAL, "Invalid memory load vaddr: 0x%08x when last=0x%08x",
                   segment->p_vaddr, next_load_address);
            return 0;
        }
        if (segment->p_memsz < segment->p_filesz) {
            debugf(CRITICAL, "Invalid memsz 0x%08x < filesz 0x%08x", segment->p_memsz, segment->p_filesz);
            return 0;
        }

        visitor(segment->p_vaddr, kernel + segment->p_offset, segment->p_filesz, segment->p_memsz, segment->p_flags);

        next_load_address = segment->p_vaddr + segment->p_memsz;
    }

    return next_load_address;
}