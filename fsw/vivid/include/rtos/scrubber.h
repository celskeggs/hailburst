#ifndef FSW_VIVID_RTOS_SCRUBBER_H
#define FSW_VIVID_RTOS_SCRUBBER_H

#include <stdbool.h>

#include <rtos/config.h>
#include <hal/clip.h>
#include <hal/watchdog.h>
#include <synch/config.h>

// VIVID_SCRUBBER_COPIES is defined in rtos/config.h
// scrubber_pend_t is defined in task.h

typedef const struct {
    struct scrubber_copy_mut {
        // TODO: figure out a way to repair kernel_elf_rom... are we *certain* this can't be part of the build?
        void        *kernel_elf_rom;
        uint64_t     iteration;
        uint8_t     *next_scrubbed_address;
    } *mut;
    uint8_t            copy_id;
    watchdog_aspect_t *aspect;
} scrubber_copy_t;

void scrubber_main_clip(scrubber_copy_t *sc);

macro_define(SCRUBBER_REGISTER) {
    static_repeat(VIVID_SCRUBBER_COPIES, s_copy_id) {
        // define a separate aspect for each scrubber copy, because they are REDUNDANT, not REPLICATED!
        WATCHDOG_ASPECT(symbol_join(scrubber, s_copy_id, aspect),
                        /* if not in strict mode, allow enough time to repair one scrubber with the other */
                        PP_IF_ELSE(CONFIG_SYNCH_MODE_STRICT == 1, CLOCK_NS_PER_SEC / 2, CLOCK_NS_PER_SEC),
                        1);
        struct scrubber_copy_mut symbol_join(scrubber, s_copy_id, mutable) = {
            .kernel_elf_rom = NULL,
            .iteration = 0,
            .next_scrubbed_address = NULL,
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
    static_repeat(VIVID_SCRUBBER_COPIES, s_copy_id) {
        CLIP_SCHEDULE(symbol_join(scrubber, s_copy_id, clip), 100)
    }
}

macro_define(SCRUBBER_WATCH) {
    static_repeat(VIVID_SCRUBBER_COPIES, s_copy_id) {
        &symbol_join(scrubber, s_copy_id, aspect),
    }
}

void scrubber_set_kernel(void *kernel_elf_rom);

void scrubber_start_pend(scrubber_pend_t *pend);
bool scrubber_is_pend_done(scrubber_pend_t *pend);

#endif /* FSW_VIVID_RTOS_SCRUBBER_H */
