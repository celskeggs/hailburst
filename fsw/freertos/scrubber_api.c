#include <elf/elf.h>
#include <rtos/scrubber.h>
#include <hal/atomic.h>
#include <hal/debug.h>
#include <hal/thread.h>

SCRUBBER_REGISTER(scrubber_1);
SCRUBBER_REGISTER(scrubber_2);

static uint64_t start_scrub_wait(struct scrubber_task_data *scrubber) {
    assert(scrubber != NULL);

    // if we're currently in an iteration, consider the 'start iteration' to be the next one; otherwise, if we're
    // waiting for an iteration, consider the 'start iteration' to be the one that's about to start.
    uint64_t start_iteration = (atomic_load_relaxed(scrubber->iteration) + 1) & ~1;

    // encourage the scrubber to start a cycle immediately
    task_rouse(scrubber->scrubber_task);

    return start_iteration;
}

static bool scrubber_done(struct scrubber_task_data *scrubber, uint64_t start_iteration) {
    return atomic_load_relaxed(scrubber->iteration) >= start_iteration + 2;
}

void scrubber_cycle_wait(void) {
    uint64_t iteration_1 = start_scrub_wait(&scrubber_1);
    uint64_t iteration_2 = start_scrub_wait(&scrubber_2);

    int max_attempts = 200; // wait at most two seconds, regardless.
    // wait until the iteration ends.
    while (!scrubber_done(&scrubber_1, iteration_1) && !scrubber_done(&scrubber_2, iteration_2)) {
        taskYIELD();
        max_attempts -= 1;
        if (max_attempts <= 0) {
            // this whole thing is a heuristic, anyway. better to not sleep forever than to insist on a scrub cycle
            // DEFINITELY having completed.
            break;
        }
    }
}

void scrubber_set_kernel(void *kernel_elf_rom) {
    assert(kernel_elf_rom != NULL);

    assert(scrubber_1.kernel_elf_rom == NULL);
    scrubber_1.kernel_elf_rom = kernel_elf_rom;
    assert(scrubber_2.kernel_elf_rom == NULL);
    scrubber_2.kernel_elf_rom = kernel_elf_rom;
}
