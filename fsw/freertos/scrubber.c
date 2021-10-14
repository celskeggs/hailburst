#include <unistd.h>

#include <elf/elf.h>
#include <rtos/scrubber.h>
#include <hal/thread.h>
#include <fsw/debug.h>

static bool scrubber_initialized;
static thread_t scrubber_thread;

enum {
    MEMORY_LOW = 0x40000000,
};

static void scrub_segment(uintptr_t vaddr, void *load_source, size_t filesz, size_t memsz, uint32_t flags) {
    if (flags & PF_W) {
        debugf("[scrubber] skipping scrub of writable segment at vaddr=0x%08x (filesz=0x%08x, memsz=0x%08x)",
               vaddr, filesz, memsz);
    } else {
        debugf("[scrubber] scrubbing read-only segment at vaddr=0x%08x (filesz=0x%08x, memsz=0x%08x)",
               vaddr, filesz, memsz);
        assert(memsz == filesz); // no BSS here, presumably?

        uint8_t *scrub_active   = (uint8_t *) vaddr;
        uint8_t *scrub_baseline = (uint8_t *) load_source;

        size_t corrections = 0;

        for (size_t i = 0; i < filesz; i++) {
            if (scrub_active[i] != scrub_baseline[i]) {
                if (corrections == 0) {
                    debugf("[scrubber] detected mismatch; beginning corrections");
                }
                scrub_active[i] = scrub_baseline[i];
                corrections++;
            }
        }

        if (corrections > 0) {
            debugf("[scrubber] summary for current segment: %u bytes corrected", corrections);
        }
    }
}

static void *scrubber_mainloop(void *opaque) {
    uint8_t *kernel_elf_rom = (uint8_t *) opaque;

    for (;;) {
        debugf("[scrubber] beginning cycle (baseline kernel ELF at 0x%08x)...", kernel_elf_rom);

        if (!elf_validate_header(kernel_elf_rom, debugf)) {
            debugf("[scrubber] header validation failed; halting scrubber.");
            return NULL;
        }

        if (elf_scan_load_segments(kernel_elf_rom, debugf, MEMORY_LOW, scrub_segment) == 0) {
            debugf("[scrubber] segment scan failed; halting scrubber.");
            return NULL;
        }

        debugf("[scrubber] scrub cycle complete.");

        // scrub about once per second
        usleep(1000000);
    }
}

void scrubber_init(void *kernel_elf_rom) {
    assert(!scrubber_initialized);
    scrubber_initialized = true;

    thread_create(&scrubber_thread, "scrubber", PRIORITY_IDLE, scrubber_mainloop, kernel_elf_rom);
}
