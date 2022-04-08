#ifndef FSW_VIVID_RTOS_SCRUBBER_H
#define FSW_VIVID_RTOS_SCRUBBER_H

#include <stdbool.h>

#include <hal/thread.h>
#include <hal/watchdog.h>

// SCRUBBER_COPIES and scrubber_pend_t are defined in task.h

typedef const struct {
    struct scrubber_copy_mut {
        // TODO: figure out a way to repair kernel_elf_rom... are we *certain* this can't be part of the build?
        void        *kernel_elf_rom;
        uint64_t     iteration;
        uint8_t     *next_scrubbed_address;
        local_time_t next_cycle_time;
        bool         encourage_immediate_cycle;
    } *mut;
    uint8_t            copy_id;
    watchdog_aspect_t *aspect;
} scrubber_copy_t;

void scrubber_main_clip(scrubber_copy_t *sc);

macro_define(SCRUBBER_REGISTER) {
    static_repeat(SCRUBBER_COPIES, s_copy_id) {
        // define a separate aspect for each scrubber copy, because they are REDUNDANT, not REPLICATED!
        WATCHDOG_ASPECT(symbol_join(scrubber, s_copy_id, aspect), 2 * CLOCK_NS_PER_SEC, 1);
        struct scrubber_copy_mut symbol_join(scrubber, s_copy_id, mutable) = {
            .kernel_elf_rom = NULL,
            .iteration = 0,
            .next_scrubbed_address = NULL,
            .next_cycle_time = 0,
            .encourage_immediate_cycle = false,
        };
        scrubber_copy_t symbol_join(scrubber, s_copy_id) = {
            .mut = &symbol_join(scrubber, s_copy_id, mutable),
            .copy_id = s_copy_id,
            .aspect = &symbol_join(scrubber, s_copy_id, aspect),
        };
        CLIP_REGISTER(symbol_join(scrubber, s_copy_id, clip), scrubber_main_clip, &symbol_join(scrubber, s_copy_id));
    }
}

macro_define(SCRUBBER_SCHEDULE) {
    static_repeat(SCRUBBER_COPIES, s_copy_id) {
        CLIP_SCHEDULE(symbol_join(scrubber, s_copy_id, clip), 100)
    }
}

macro_define(SCRUBBER_WATCH) {
    static_repeat(SCRUBBER_COPIES, s_copy_id) {
        &symbol_join(scrubber, s_copy_id, aspect),
    }
}

void scrubber_set_kernel(void *kernel_elf_rom);

void scrubber_start_pend(scrubber_pend_t *pend);
bool scrubber_is_pend_done(scrubber_pend_t *pend);

#endif /* FSW_VIVID_RTOS_SCRUBBER_H */
