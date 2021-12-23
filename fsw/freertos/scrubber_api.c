#include <unistd.h>

#include <elf/elf.h>
#include <rtos/scrubber.h>
#include <hal/atomic.h>
#include <hal/thread.h>
#include <fsw/debug.h>

bool scrubber_initialized = false;
void *scrubber_kernel_elf_rom = NULL;
uint64_t scrubber_iteration = 0;
semaphore_t scrubber_wake;
semaphore_t scrubber_idle_wake;

void scrubber_cycle_wait(bool is_idle) {
    // if we're currently in an iteration, consider the 'start iteration' to be the next one; otherwise, if we're
    // waiting for an iteration, consider the 'start iteration' to be the one that's about to start.
    uint64_t start_iteration = (atomic_load_relaxed(scrubber_iteration) + 1) & ~1;

    // force the scrubber to start a cycle NOW
    if (atomic_load(scrubber_initialized)) {
        // ignore duplicates, because that indicates a cycle has already been requested
        (void) semaphore_give(&scrubber_wake);
    }

    int max_attempts = 200; // wait at most two seconds, regardless.
    // wait until the iteration ends.
    while (atomic_load_relaxed(scrubber_iteration) < start_iteration + 2) {
        if (is_idle) {
            // we initialize the scrubber before the IDLE task, so this is safe.
            assert(scrubber_initialized == true);
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
    assert(scrubber_kernel_elf_rom == NULL);
    scrubber_kernel_elf_rom = kernel_elf_rom;
}
