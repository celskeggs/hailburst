#include <unistd.h>

#include <elf/elf.h>
#include <rtos/scrubber.h>
#include <hal/atomic.h>
#include <hal/thread.h>
#include <fsw/debug.h>

extern struct scrubber_task_data scrubber_data_1, scrubber_data_2;
// singly-instanced because there is only one IDLE task
bool        wake_inited;
semaphore_t scrubber_idle_wake;

static uint64_t start_scrub_wait(struct scrubber_task_data *scrubber) {
    assert(scrubber != NULL);

    // if we're currently in an iteration, consider the 'start iteration' to be the next one; otherwise, if we're
    // waiting for an iteration, consider the 'start iteration' to be the one that's about to start.
    uint64_t start_iteration = (atomic_load_relaxed(scrubber->iteration) + 1) & ~1;

    // force the scrubber to start a cycle NOW
    if (atomic_load(scrubber->initialized)) {
        // ignore duplicates, because that indicates a cycle has already been requested
        (void) semaphore_give(&scrubber->wake);
    } else {
        // if not initialized yet, that's fine; the scrubber will immediately start a cycle when it does initialize.
    }

    return start_iteration;
}

static bool scrubber_done(struct scrubber_task_data *scrubber, uint64_t start_iteration) {
    return atomic_load_relaxed(scrubber->iteration) >= start_iteration + 2;
}

void scrubber_cycle_wait(bool is_idle) {
    uint64_t iteration_1 = start_scrub_wait(&scrubber_data_1);
    uint64_t iteration_2 = start_scrub_wait(&scrubber_data_2);

    int max_attempts = 200; // wait at most two seconds, regardless.
    // wait until the iteration ends.
    while (!scrubber_done(&scrubber_data_1, iteration_1) && !scrubber_done(&scrubber_data_2, iteration_2)) {
        if (is_idle) {
            // we initialize the scrubber before the IDLE task, so this is safe.
            assert(wake_inited);
            // need a dedicated wakeup for IDLE task recovery, because otherwise there is a period of time without any
            // idle-priority task running. but we can't do this for everything, or the signals would clash.
            semaphore_take(&scrubber_idle_wake);
        } else {
            usleep(10 * 1000); // wait about 10 milliseconds and then recheck
        }
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

    assert(!wake_inited);
    semaphore_init(&scrubber_idle_wake);
    atomic_store(wake_inited, true);

    assert(scrubber_data_1.kernel_elf_rom == NULL);
    scrubber_data_1.kernel_elf_rom = kernel_elf_rom;
    assert(scrubber_data_2.kernel_elf_rom == NULL);
    scrubber_data_2.kernel_elf_rom = kernel_elf_rom;
}
