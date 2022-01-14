#ifndef FSW_FREERTOS_RTOS_SCRUBBER_H
#define FSW_FREERTOS_RTOS_SCRUBBER_H

#include <stdbool.h>

#include <hal/thread.h>

struct scrubber_task_data {
    void *kernel_elf_rom;
    uint64_t iteration;
    thread_t scrubber_task;
};

// for scrubber->idle_task notifications
extern TCB_t idle_task;

void scrubber_set_kernel(void *kernel_elf_rom);
// wait until the next (unstarted) scrubber cycle completes.
void scrubber_cycle_wait(void);

#endif /* FSW_FREERTOS_RTOS_SCRUBBER_H */
