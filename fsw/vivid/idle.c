#include <rtos/config.h>
#include <hal/atomic.h>
#include <hal/clip.h>

// not ordinarily used; only here for testing without the partition scheduler.
// if we don't set a minimum IDLE time, the simulation runs *really* slow.
#if ( VIVID_PARTITION_SCHEDULE_ENFORCEMENT <= 1 && VIVID_PARTITION_SCHEDULE_MINIMUM_CYCLE_TIME > 0 )

void idle_clip(void) {
    uint64_t end_of_cycle = schedule_epoch_start + VIVID_PARTITION_SCHEDULE_MINIMUM_CYCLE_TIME;

#if ( VIVID_PARTITION_SCHEDULE_ENFORCEMENT == 0 )
    // always set the next callback time
    arm_set_cntp_cval(end_of_cycle / CLOCK_PERIOD_NS);
    // set the enable bit and don't set the mask bit
    arm_set_cntp_ctl(ARM_TIMER_ENABLE);
#else
    uint64_t current_end_of_clip = arm_get_cntp_cval() * CLOCK_PERIOD_NS;
    if (end_of_cycle < current_end_of_clip) {
        // set the next callback time only if it's sooner
        arm_set_cntp_cval(end_of_cycle / CLOCK_PERIOD_NS);
    }
#endif

    // fake the completion of the clip
    clip_t *clip = schedule_get_clip();
    clip->mut->clip_next_tick += 1;
    atomic_store(clip->mut->clip_running, false);
    clip->mut->needs_start = false;

    schedule_wait_for_interrupt();
}

#endif
