#include <elf/elf.h>
#include <rtos/scrubber.h>
#include <hal/atomic.h>
#include <hal/clip.h>
#include <hal/debug.h>
#include <hal/init.h>
#include <hal/thread.h>

enum {
    MEMORY_LOW = 0x40000000,

    SCRUBBER_ESCAPE_CHECK_INTERVAL = 128,
    SCRUBBER_ESCAPE_TIMEOUT        = 4 * CLOCK_NS_PER_US,
    SCRUBBER_CYCLE_DELAY           = 400 * CLOCK_NS_PER_MS, // must be small enough to work well with watchdog timeouts
};

static void scrub_segment(uintptr_t vaddr, void *load_source, size_t filesz, size_t memsz, uint32_t flags,
                          void *opaque) {
    struct scrubber_task_data *local = (struct scrubber_task_data *) opaque;

    uint8_t *scrub_active   = (uint8_t *) vaddr;
    uint8_t *scrub_baseline = (uint8_t *) load_source;

    size_t start_offset = (local->next_scrubbed_address == NULL ? 0 : local->next_scrubbed_address - scrub_active);

    if (start_offset >= filesz) {
        // scrubbing something else
        return;
    }

    if (flags & PF_W) {
        assert(local->next_scrubbed_address == NULL);
        debugf(DEBUG, "Skipping scrub of writable segment at vaddr=0x%08x (filesz=0x%08x, memsz=0x%08x)",
               vaddr, filesz, memsz);
    } else {
        debugf(DEBUG, "Scrubbing read-only segment at vaddr=0x%08x (filesz=0x%08x, memsz=0x%08x) from offset=0x%08x, "
               "time remaining=%uns", vaddr, filesz, memsz, start_offset, clip_remaining_ns());
        assert(memsz == filesz); // no BSS here, presumably?

        size_t corrections = 0;

        size_t i;
        for (i = start_offset; i < filesz; i++) {
            if (i % SCRUBBER_ESCAPE_CHECK_INTERVAL == 0 && clip_remaining_ns() < SCRUBBER_ESCAPE_TIMEOUT) {
                debugf(TRACE, "Scrubber pausing remainder of check; not enough time left to complete cycle now.");
                break;
            }
            if (scrub_active[i] != scrub_baseline[i]) {
                if (corrections == 0) {
                    debugf(WARNING, "Detected mismatch in read-only memory. Beginning corrections.");
                }
                scrub_active[i] = scrub_baseline[i];
                corrections++;
            }
        }

        if (corrections > 0) {
            debugf(WARNING, "Summary for current scrubber step: %u bytes corrected.", corrections);
        }

        if (i == filesz) {
            // continue to next segment
            local->next_scrubbed_address = NULL;
        } else {
            local->next_scrubbed_address = &scrub_active[i];
        }
    }
}

void scrubber_main_clip(struct scrubber_task_data *local) {
    assert(local != NULL);
    assert(local->kernel_elf_rom != NULL);

    local_time_t now = timer_now_ns();

    if (clip_is_restart()) {
        debugf(DEBUG, "Reset scrubber state due to restart.");
        local->next_scrubbed_address = NULL;
        local->next_cycle_time = now;
    }

    bool watchdog_ok = false;

    if (local->next_scrubbed_address != NULL
            || now >= local->next_cycle_time
            || now < local->next_cycle_time - SCRUBBER_CYCLE_DELAY
            || atomic_load_relaxed(local->encourage_immediate_cycle) == true) {

        if (local->next_scrubbed_address == NULL) {
            debugf(DEBUG, "Beginning scrub cycle (baseline kernel ELF at 0x%08x)...",
                          (uintptr_t) local->kernel_elf_rom);

            local->encourage_immediate_cycle = false;

            if (!elf_validate_header(local->kernel_elf_rom)) {
                restartf("Header validation failed; resetting scrubber.");
            }
        }

        void *last = local->next_scrubbed_address;

        if (elf_scan_load_segments(local->kernel_elf_rom, MEMORY_LOW, scrub_segment, local) == 0) {
            restartf("Segment scan failed; resetting scrubber.");
        }

        if (last != NULL && last == local->next_scrubbed_address) {
            restartf("No scan progress made; resetting scrubber.");
        }

        if (local->next_scrubbed_address == NULL) {
            // completed iteration
            atomic_store_relaxed(local->iteration, local->iteration + 1);

            debugf(DEBUG, "Scrub cycle complete.");

            watchdog_ok = true;

            local->next_cycle_time = now + SCRUBBER_CYCLE_DELAY;
        }
    }

    watchdog_indicate(local->aspect, 0, watchdog_ok);
}
