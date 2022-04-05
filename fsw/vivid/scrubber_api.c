#include <elf/elf.h>
#include <rtos/scrubber.h>
#include <hal/atomic.h>
#include <hal/debug.h>
#include <hal/thread.h>

SCRUBBER_REGISTER(scrubber_1);
SCRUBBER_REGISTER(scrubber_2);

static uint64_t start_scrub_wait(struct scrubber_task_data *scrubber) {
    assert(scrubber != NULL);

    uint64_t start_iteration = atomic_load_relaxed(scrubber->iteration);

    // encourage the scrubber to start a cycle immediately
    atomic_store_relaxed(scrubber->encourage_immediate_cycle, true);

    return start_iteration;
}

static bool scrubber_done(struct scrubber_task_data *scrubber, uint64_t start_iteration) {
    return atomic_load_relaxed(scrubber->iteration) > start_iteration;
}

void scrubber_start_pend(scrubber_pend_t *pend) {
    assert(pend != NULL);
    pend->iteration_1 = start_scrub_wait(&scrubber_1);
    pend->iteration_2 = start_scrub_wait(&scrubber_2);
    pend->max_attempts = 200;
}

bool scrubber_is_pend_done(scrubber_pend_t *pend) {
    assert(pend != NULL);
    // explanation of max_attempts: this whole thing is a heuristic. better to not sleep forever than to insist on a
    // scrub cycle DEFINITELY having completed.
    if (pend->max_attempts > 0) {
        pend->max_attempts -= 1;
    }
    return (pend->max_attempts == 0) || scrubber_done(&scrubber_1, pend->iteration_1)
                                     || scrubber_done(&scrubber_2, pend->iteration_2);
}

void scrubber_cycle_wait(void) {
    scrubber_pend_t pend;
    scrubber_start_pend(&pend);

    while (!scrubber_is_pend_done(&pend)) {
        task_yield();
    }
}

void scrubber_set_kernel(void *kernel_elf_rom) {
    assert(kernel_elf_rom != NULL);

    assert(scrubber_1.kernel_elf_rom == NULL);
    scrubber_1.kernel_elf_rom = kernel_elf_rom;
    assert(scrubber_2.kernel_elf_rom == NULL);
    scrubber_2.kernel_elf_rom = kernel_elf_rom;
}
