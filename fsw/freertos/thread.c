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
        debugf(WARNING, "Task %s resuming after scrubber cycle completion.", state->pcTaskName);
    }

    /* clear crash flag */
    state->mut->recursive_exception = false;

    state->start_routine(state->start_arg);

    restartf("Task main loop unexpectedly returned.");
}

void clip_play_direct(void) {
    thread_t clip = task_get_current();

    if (clip->mut->hit_restart) {
        // clear crash flag
        clip->mut->recursive_exception = false;

        // pend started in restart_current_task() to simplify this logic for us.
        if (!scrubber_is_pend_done(&clip->mut->clip_pend)) {
            // Go back to the top next scheduling period.
            task_yield();
            abortf("Clips should never return from yield!");
        }
        debugf(WARNING, "Clip %s resuming after scrubber cycle completion.", clip->pcTaskName);
        clip->mut->hit_restart = false;
        clip->mut->clip_next_tick = task_tick_index();
        clip->mut->needs_start = true;
    } else if (atomic_load(clip->mut->clip_running)) {
        malfunctionf("Clip %s did not have a chance to complete by the end of its execution!", clip->pcTaskName);
        clip->mut->needs_start = true;
    } else {
        uint32_t now = task_tick_index();
        if (now != clip->mut->clip_next_tick) {
            malfunctionf("Clip %s desynched from timeline. Tick found to be %u instead of %u.",
                         clip->pcTaskName, now, clip->mut->clip_next_tick);
            clip->mut->needs_start = true;
        }
    }

    atomic_store(clip->mut->clip_running, true);

    // actual execution body
    clip->start_routine(clip->start_arg);

    // should never fail, because the clip would have been rescheduled (and therefore restart) if this happened!
    assert(task_tick_index() == clip->mut->clip_next_tick);
    clip->mut->clip_next_tick += 1;

    assert(clip->mut->clip_running == true);
    atomic_store(clip->mut->clip_running, false);
    clip->mut->needs_start = false;

    // yield until we are rescheduled, and start from the beginning.
    task_yield();
    abortf("It should be impossible for any clip to ever resume from yield!");
}
