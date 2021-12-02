#ifndef FSW_FREERTOS_RTOS_SCRUBBER_H
#define FSW_FREERTOS_RTOS_SCRUBBER_H

#include <stdbool.h>

void scrubber_init(void *kernel_elf_rom);
// wait until the next (unstarted) scrubber cycle completes.
// set is_idle to true iff called from the IDLE task
void scrubber_cycle_wait(bool is_idle);

#endif /* FSW_FREERTOS_RTOS_SCRUBBER_H */
