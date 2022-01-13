#include <elf/elf.h>
#include <rtos/scrubber.h>
#include <hal/atomic.h>
#include <hal/thread.h>
#include <fsw/debug.h>
#include <fsw/init.h>

extern struct scrubber_task_data scrubber_data_local;

enum {
    MEMORY_LOW = 0x40000000,
};

static void scrub_segment(uintptr_t vaddr, void *load_source, size_t filesz, size_t memsz, uint32_t flags) {
    if (flags & PF_W) {
        debugf(DEBUG, "skipping scrub of writable segment at vaddr=0x%08x (filesz=0x%08x, memsz=0x%08x)",
               vaddr, filesz, memsz);
    } else {
        debugf(DEBUG, "scrubbing read-only segment at vaddr=0x%08x (filesz=0x%08x, memsz=0x%08x)",
               vaddr, filesz, memsz);
        assert(memsz == filesz); // no BSS here, presumably?

        uint8_t *scrub_active   = (uint8_t *) vaddr;
        uint8_t *scrub_baseline = (uint8_t *) load_source;

        size_t corrections = 0;

        for (size_t i = 0; i < filesz; i++) {
            if (scrub_active[i] != scrub_baseline[i]) {
                if (corrections == 0) {
                    debugf(WARNING, "detected mismatch; beginning corrections");
                }
                scrub_active[i] = scrub_baseline[i];
                corrections++;
            }
        }

        if (corrections > 0) {
            debugf(WARNING, "summary for current segment: %u bytes corrected", corrections);
        }
    }
}

static void scrubber_mainloop(void) {
    for (;;) {
        assert(scrubber_data_local.kernel_elf_rom != NULL);

        debugf(DEBUG, "beginning cycle (baseline kernel ELF at 0x%08x)...",
                      (uintptr_t) scrubber_data_local.kernel_elf_rom);

        atomic_store_relaxed(scrubber_data_local.iteration, scrubber_data_local.iteration | 1);

        if (!elf_validate_header(scrubber_data_local.kernel_elf_rom)) {
            debugf(CRITICAL, "header validation failed; halting scrubber.");
            break;
        }

        if (elf_scan_load_segments(scrubber_data_local.kernel_elf_rom, MEMORY_LOW, scrub_segment) == 0) {
            debugf(CRITICAL, "segment scan failed; halting scrubber.");
            break;
        }

        atomic_store_relaxed(scrubber_data_local.iteration, scrubber_data_local.iteration + 1);
        // just in case the idle task is dozing to wait for a scrub cycle
        task_rouse(&idle_task);

        debugf(DEBUG, "scrub cycle complete.");

        // scrub about once per second, or more often if requested. (don't care which.)
        (void) task_doze_timed(TIMER_NS_PER_SEC);
    }
}

TASK_REGISTER(scrubber_task, "scrubber", PRIORITY_IDLE, scrubber_mainloop, NULL, RESTARTABLE);

struct scrubber_task_data scrubber_data_local = {
    .kernel_elf_rom = NULL,
    .iteration = 0,
    .scrubber_task = &scrubber_task,
};
