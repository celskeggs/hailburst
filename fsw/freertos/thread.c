#include <FreeRTOS.h>
#include <task.h>

#include <rtos/crash.h>
#include <rtos/scrubber.h>
#include <hal/atomic.h>
#include <hal/clip.h>
#include <hal/debug.h>
#include <hal/init.h>
#include <hal/thread.h>
#include <synch/config.h>

void task_entrypoint(TCB_t *state) {
    if (state->mut->hit_restart) {
        debugf(WARNING, "Pending restart on next scrubber cycle.");
        scrubber_cycle_wait();
    }

    /* clear crash flag */
    state->mut->recursive_exception = false;

    state->start_routine(state->start_arg);

    restartf("Task main loop unexpectedly returned.");
}

void clip_play_direct(void) {
    thread_t clip = task_get_current();

    if (clip->mut->hit_restart) {
        debugf(WARNING, "Clip %s resuming immediately; not waiting for scrubber cycle.", clip->pcTaskName);
        clip->mut->hit_restart = false;
        clip->mut->clip_next_tick = task_tick_index();
    } else if (atomic_load(clip->mut->clip_running)) {
        miscomparef("Clip %s did not have a chance to complete by the end of its execution!", clip->pcTaskName);
    } else {
        uint32_t now = task_tick_index();
        if (now != clip->mut->clip_next_tick) {
            miscomparef("Clip %s desynched from timeline. Tick found to be %u instead of %u.",
                        clip->pcTaskName, now, clip->mut->clip_next_tick);
        }
    }

    atomic_store(clip->mut->clip_running, true);

    // clear crash flag
    clip->mut->recursive_exception = false;

    // actual execution body
    clip->start_routine(clip->start_arg);

    // should never fail, because the clip would have been rescheduled (and therefore restart) if this happened!
    assert(task_tick_index() == clip->mut->clip_next_tick);
    clip->mut->clip_next_tick += 1;

    assert(clip->mut->clip_running == true);
    atomic_store(clip->mut->clip_running, false);

    // yield until we are rescheduled, and start from the beginning.
    task_yield();
    abortf("It should be impossible for any clip to ever resume from yield!");
}
