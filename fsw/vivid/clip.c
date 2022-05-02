#include <rtos/config.h>
#include <rtos/scrubber.h>
#include <hal/atomic.h>
#include <hal/clip.h>
#include <hal/debug.h>
#include <hal/init.h>
#include <synch/strict.h>

__attribute__((noreturn)) void clip_play_direct(void (*entrypoint)(void*)) {
    clip_t *clip = schedule_get_clip();

    if (clip->mut->hit_restart) {
        // clear crash flag
        clip->mut->recursive_exception = false;

#if ( VIVID_RECOVERY_WAIT_FOR_SCRUBBER == 1 )
        // pend started in restart_current_clip() to simplify this logic for us.
        if (!scrubber_is_pend_done(&clip->mut->clip_pend)) {
            // Go back to the top next scheduling period.
            schedule_yield();
            abortf("Clips should never return from yield!");
        }
        debugf(WARNING, "Clip %s resuming after scrubber cycle completion.", clip->label);
#endif
        clip->mut->hit_restart = false;
        clip->mut->clip_next_tick = schedule_tick_index();
        clip->mut->needs_start = true;
    } else if (atomic_load(clip->mut->clip_running)) {
        malfunctionf("Clip %s did not have a chance to complete by the end of its execution!", clip->label);
        clip->mut->needs_start = true;
    } else {
        uint32_t now = schedule_tick_index();
        if (now != clip->mut->clip_next_tick) {
            malfunctionf("Clip %s desynched from timeline. Tick found to be %u instead of %u.",
                         clip->label, now, clip->mut->clip_next_tick);
            clip->mut->needs_start = true;
        }
    }

    atomic_store(clip->mut->clip_running, true);

    // actual execution body
    entrypoint(clip->start_arg);

    // should never fail, because the clip would have been rescheduled (and therefore restart) if this happened!
    assert(schedule_tick_index() == clip->mut->clip_next_tick);
    clip->mut->clip_next_tick += 1;

    assert(clip->mut->clip_running == true);
    atomic_store(clip->mut->clip_running, false);
    clip->mut->needs_start = false;

    int64_t elapsed = timer_now_ns() - schedule_period_start;
    if (elapsed > 0 && (uint64_t) elapsed > clip->mut->clip_max_nanos) {
        clip->mut->clip_max_nanos = elapsed;
        debugf(TRACE, "New longest clip duration for %s: %u.%03u microseconds.",
               clip->label, elapsed / 1000, elapsed % 1000);
    }

    // yield until we are rescheduled, and start from the beginning.
    schedule_yield();
    abortf("It should be impossible for any clip to ever resume from yield!");
}
