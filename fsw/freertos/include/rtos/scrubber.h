#ifndef FSW_FREERTOS_RTOS_SCRUBBER_H
#define FSW_FREERTOS_RTOS_SCRUBBER_H

void scrubber_init(void *kernel_elf_rom);
// wait until the next (unstarted) scrubber cycle completes.
void scrubber_cycle_wait(void);

#endif /* FSW_FREERTOS_RTOS_SCRUBBER_H */
