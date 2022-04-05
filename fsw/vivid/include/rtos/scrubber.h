#ifndef FSW_VIVID_RTOS_SCRUBBER_H
#define FSW_VIVID_RTOS_SCRUBBER_H

#include <stdbool.h>

#include <hal/thread.h>
#include <hal/watchdog.h>

struct scrubber_task_data {
    void              *kernel_elf_rom;
    uint64_t           iteration;
    watchdog_aspect_t *aspect;
    uint8_t           *next_scrubbed_address;
    local_time_t       next_cycle_time;
    bool               encourage_immediate_cycle;
};

// scrubber_pend_t defined in task.h

void scrubber_main_clip(struct scrubber_task_data *local);

macro_define(SCRUBBER_REGISTER, s_ident) {
    WATCHDOG_ASPECT(symbol_join(s_ident, aspect), 1);
    struct scrubber_task_data s_ident = {
        .kernel_elf_rom = NULL,
        .iteration = 0,
        .aspect = &symbol_join(s_ident, aspect),
        .next_scrubbed_address = NULL,
        .next_cycle_time = 0,
        .encourage_immediate_cycle = false,
    };
    CLIP_REGISTER(symbol_join(s_ident, clip), scrubber_main_clip, &s_ident)
}

macro_define(SCRUBBER_SCHEDULE, s_ident) {
    CLIP_SCHEDULE(symbol_join(s_ident, clip), 100)
}

macro_define(SCRUBBER_WATCH, s_ident) {
    &symbol_join(s_ident, aspect),
}

void scrubber_set_kernel(void *kernel_elf_rom);

void scrubber_start_pend(scrubber_pend_t *pend);
bool scrubber_is_pend_done(scrubber_pend_t *pend);

// wait until the next (unstarted) scrubber cycle completes.
void scrubber_cycle_wait(void);

#endif /* FSW_VIVID_RTOS_SCRUBBER_H */
