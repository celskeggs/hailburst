#include <unistd.h>

#include <elf/elf.h>
#include <rtos/scrubber.h>
#include <hal/atomic.h>
#include <hal/thread.h>
#include <fsw/debug.h>

static bool scrubber_initialized;
static thread_t scrubber_thread;

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
                    debugf(CRITICAL, "detected mismatch; beginning corrections");
                }
                scrub_active[i] = scrub_baseline[i];
                corrections++;
            }
        }

        if (corrections > 0) {
            debugf(CRITICAL, "summary for current segment: %u bytes corrected", corrections);
        }
    }
}

static uint64_t scrubber_iteration = 0;
static semaphore_t scrubber_wake;

static void *scrubber_mainloop(void *opaque) {
    uint8_t *kernel_elf_rom = (uint8_t *) opaque;

    for (;;) {
        debugf(DEBUG, "beginning cycle (baseline kernel ELF at 0x%08x)...", (uintptr_t) kernel_elf_rom);

        atomic_store_relaxed(scrubber_iteration, scrubber_iteration | 1);

        if (!elf_validate_header(kernel_elf_rom)) {
            debugf(CRITICAL, "header validation failed; halting scrubber.");
            return NULL;
        }

        if (elf_scan_load_segments(kernel_elf_rom, MEMORY_LOW, scrub_segment) == 0) {
            debugf(CRITICAL, "segment scan failed; halting scrubber.");
            return NULL;
        }

        atomic_store_relaxed(scrubber_iteration, scrubber_iteration + 1);

        debugf(DEBUG, "scrub cycle complete.");

        // scrub about once per second, or more often if requested. (don't care which.)
        (void) semaphore_take_timed(&scrubber_wake, TIMER_NS_PER_SEC);
    }
}

void scrubber_cycle_wait(void) {
    // if we're currently in an iteration, consider the 'start iteration' to be the next one; otherwise, if we're
    // waiting for an iteration, consider the 'start iteration' to be the one that's about to start.
    uint64_t start_iteration = (atomic_load_relaxed(scrubber_iteration) + 1) & ~1;

    // force the scrubber to start a cycle NOW
    if (atomic_load(scrubber_initialized)) {
        // ignore duplicates, because that indicates a cycle has already been requested
        (void) semaphore_give(&scrubber_wake);
    }

    int max_attempts = 200; // wait at most two seconds, regardless.
    // wait until the iteration ends.
    while (atomic_load_relaxed(scrubber_iteration) < start_iteration + 2) {
        usleep(10 * 1000); // wait about 10 milliseconds and then recheck
        max_attempts -= 1;
        if (max_attempts <= 0) {
            // this whole thing is a heuristic, anyway. better to not sleep forever than to insist on a scrub cycle
            // DEFINITELY having completed.
            break;
        }
    }
}

void scrubber_init(void *kernel_elf_rom) {
    assert(!scrubber_initialized);

    semaphore_init(&scrubber_wake);
    thread_create(&scrubber_thread, "scrubber", PRIORITY_IDLE, scrubber_mainloop, kernel_elf_rom, RESTARTABLE);

    atomic_store(scrubber_initialized, true);
}
