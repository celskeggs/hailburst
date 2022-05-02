#include <elf/elf.h>
#include <rtos/config.h>
#include <rtos/scrubber.h>
#include <hal/atomic.h>
#include <hal/clip.h>
#include <hal/debug.h>
#include <hal/init.h>

enum {
    MEMORY_LOW = 0x40000000,

    SCRUBBER_ESCAPE_CHECK_INTERVAL = 128,
    SCRUBBER_ESCAPE_TIMEOUT        = 4 * CLOCK_NS_PER_US,
};

static void scrub_segment(uintptr_t vaddr, void *load_source, size_t filesz, size_t memsz, uint32_t flags,
                          void *opaque) {
    scrubber_copy_t *sc = (scrubber_copy_t *) opaque;

    uint8_t *scrub_active   = (uint8_t *) vaddr;
    uint8_t *scrub_baseline = (uint8_t *) load_source;

    size_t start_offset = (sc->mut->next_scrubbed_address == NULL ? 0 : sc->mut->next_scrubbed_address - scrub_active);

    if (start_offset >= filesz) {
        // scrubbing something else
        return;
    }

    if (flags & PF_W) {
        assert(sc->mut->next_scrubbed_address == NULL);
        debugf(TRACE, "Skipping scrub of writable segment at vaddr=0x%08x (filesz=0x%08x, memsz=0x%08x)",
               vaddr, filesz, memsz);
    } else {
        debugf(TRACE, "Scrubbing read-only segment at vaddr=0x%08x (filesz=0x%08x, memsz=0x%08x) from offset=0x%08x, "
               "time remaining=%uns", vaddr, filesz, memsz, start_offset, schedule_remaining_ns());
        assert(memsz == filesz); // no BSS here, presumably?

        size_t corrections = 0;

        assert(filesz % sizeof(uint32_t) == 0 && start_offset % sizeof(uint32_t) == 0);
        size_t i;
        for (i = start_offset; i < filesz; i += sizeof(uint32_t)) {
            if ((i / sizeof(uint32_t)) % SCRUBBER_ESCAPE_CHECK_INTERVAL == 0
                        && schedule_remaining_ns() < SCRUBBER_ESCAPE_TIMEOUT) {
                debugf(TRACE, "Scrubber pausing remainder of check; not enough time left to complete cycle now.");
                break;
            }
            uint32_t *active_ref = (uint32_t *) &scrub_active[i];
            uint32_t *baseline_ref = (uint32_t *) &scrub_baseline[i];
            if (*active_ref != *baseline_ref) {
                if (corrections == 0) {
                    debugf(WARNING, "Detected mismatch in read-only memory. Beginning corrections.");
                }
                *active_ref = *baseline_ref;
                corrections++;
            }
        }

        if (corrections > 0) {
            debugf(WARNING, "Summary for current scrubber step: %u word(s) corrected.", corrections);
        }

        if (i == filesz) {
            // continue to next segment
            sc->mut->next_scrubbed_address = NULL;
        } else {
            sc->mut->next_scrubbed_address = &scrub_active[i];
        }
    }
}

void scrubber_main_clip(scrubber_copy_t *sc) {
    assert(sc != NULL && sc->mut != NULL && sc->mut->kernel_elf_rom != NULL);

    if (clip_is_restart()) {
        debugf(DEBUG, "Reset scrubber state due to restart.");
        sc->mut->next_scrubbed_address = NULL;
    }

    if (sc->mut->next_scrubbed_address == NULL) {
        debugf(DEBUG, "Beginning scrub cycle (baseline kernel ELF at 0x%08x)...",
                      (uintptr_t) sc->mut->kernel_elf_rom);

        if (!elf_validate_header(sc->mut->kernel_elf_rom)) {
            restartf("Header validation failed; resetting scrubber.");
        }
    }

    void *last = sc->mut->next_scrubbed_address;

    if (elf_scan_load_segments(sc->mut->kernel_elf_rom, MEMORY_LOW, scrub_segment, (void *) sc) == 0) {
        restartf("Segment scan failed; resetting scrubber.");
    }

    if (last != NULL && last == sc->mut->next_scrubbed_address) {
        restartf("No scan progress made; resetting scrubber.");
    }

    bool watchdog_ok = false;

    if (sc->mut->next_scrubbed_address == NULL) {
        // completed iteration
        atomic_store_relaxed(sc->mut->iteration, sc->mut->iteration + 1);

        debugf(DEBUG, "Scrub cycle complete.");

        watchdog_ok = true;
    }

    watchdog_indicate(sc->aspect, 0, watchdog_ok);
}

#if ( VIVID_SCRUBBER_COPIES > 0 )
static uint64_t start_scrub_wait(scrubber_copy_t *scrubber) {
    assert(scrubber != NULL);

    uint64_t start_iteration = atomic_load_relaxed(scrubber->mut->iteration);
    return start_iteration;
}

static bool scrubber_done(scrubber_copy_t *scrubber, uint64_t start_iteration) {
    return atomic_load_relaxed(scrubber->mut->iteration) > start_iteration;
}
#endif

static_repeat(VIVID_SCRUBBER_COPIES, s_copy_id) {
    extern scrubber_copy_t symbol_join(scrubber, s_copy_id);
}

void scrubber_start_pend(scrubber_pend_t *pend) {
    assert(pend != NULL);
    static_repeat(VIVID_SCRUBBER_COPIES, s_copy_id) {
        pend->iteration[s_copy_id] = start_scrub_wait(&symbol_join(scrubber, s_copy_id));
    }
    pend->max_attempts = 200;
}

bool scrubber_is_pend_done(scrubber_pend_t *pend) {
    assert(pend != NULL);
    // explanation of max_attempts: this whole thing is a heuristic. better to not sleep forever than to insist on a
    // scrub cycle DEFINITELY having completed.
    if (pend->max_attempts == 0) {
        return true;
    }
    pend->max_attempts -= 1;
    static_repeat(VIVID_SCRUBBER_COPIES, s_copy_id) {
        if (scrubber_done(&symbol_join(scrubber, s_copy_id), pend->iteration[s_copy_id])) {
            return true;
        }
    }
    return false;
}

void scrubber_set_kernel(void *kernel_elf_rom) {
    assert(kernel_elf_rom != NULL);

    static_repeat(VIVID_SCRUBBER_COPIES, s_copy_id) {
        assert(symbol_join(scrubber, s_copy_id).mut->kernel_elf_rom == NULL);
        symbol_join(scrubber, s_copy_id).mut->kernel_elf_rom = kernel_elf_rom;
    }
}
