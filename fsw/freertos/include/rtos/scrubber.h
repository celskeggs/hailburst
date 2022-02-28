#ifndef FSW_FREERTOS_RTOS_SCRUBBER_H
#define FSW_FREERTOS_RTOS_SCRUBBER_H

#include <stdbool.h>

#include <hal/thread.h>

struct scrubber_task_data {
    void *kernel_elf_rom;
    uint64_t iteration;
    thread_t scrubber_task;
};

// scrubber_pend_t defined in task.h

void scrubber_mainloop(struct scrubber_task_data *local);

#define SCRUBBER_REGISTER(s_ident)                                                                                    \
    extern struct scrubber_task_data s_ident;                                                                         \
    TASK_REGISTER(s_ident ## _task, scrubber_mainloop, &s_ident, RESTARTABLE);                                        \
    struct scrubber_task_data s_ident = {                                                                             \
        .kernel_elf_rom = NULL,                                                                                       \
        .iteration      = 0,                                                                                          \
        .scrubber_task  = &s_ident ## _task,                                                                          \
    }

void scrubber_set_kernel(void *kernel_elf_rom);

void scrubber_start_pend(scrubber_pend_t *pend);
bool scrubber_is_pend_done(scrubber_pend_t *pend);

// wait until the next (unstarted) scrubber cycle completes.
void scrubber_cycle_wait(void);

#endif /* FSW_FREERTOS_RTOS_SCRUBBER_H */
