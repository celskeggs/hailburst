#ifndef FSW_FREERTOS_RTOS_SCRUBBER_H
#define FSW_FREERTOS_RTOS_SCRUBBER_H

#include <stdbool.h>

#include <hal/thread.h>

struct scrubber_task_data {
    bool initialized;
    void *kernel_elf_rom;
    uint64_t iteration;
    semaphore_t wake;
};

void scrubber_set_kernel(void *kernel_elf_rom);
// wait until the next (unstarted) scrubber cycle completes.
// set is_idle to true iff called from the IDLE task
void scrubber_cycle_wait(bool is_idle);

#endif /* FSW_FREERTOS_RTOS_SCRUBBER_H */
