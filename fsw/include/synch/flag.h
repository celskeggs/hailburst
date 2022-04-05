#ifndef FSW_SYNCH_FLAG_H
#define FSW_SYNCH_FLAG_H

#include <hal/debug.h>
#include <hal/timer.h>

enum {
    // warn again every 100 milliseconds
    FLAG_SUSTAIN_PERIOD_NS = 100 * CLOCK_NS_PER_MS,
    // note if the issue has stopped happening for an entire 10 milliseconds
    FLAG_RECOVER_PERIOD_NS =  10 * CLOCK_NS_PER_MS,
};

typedef struct {
    bool sustained;
    local_time_t last_raised;
    local_time_t last_sustained;
} flag_t;

#define FLAG_INITIALIZER ((flag_t) { .sustained = false, .last_raised = 0, .last_sustained = 0 })

static inline bool flag_raise_check(flag_t *flag) {
    assert(flag != NULL);
    local_time_t now = timer_now_ns();
    flag->last_raised = now;
    // we include 'now < last_sustained' here so that, if last_sustained is corrupted, this won't get stuck
    // indefinitely.
    if (flag->sustained == false || now < flag->last_sustained
                                 || now >= flag->last_sustained + FLAG_SUSTAIN_PERIOD_NS) {
        flag->sustained = true;
        flag->last_sustained = now;
        return true;
    }
    return false;
}

static inline bool flag_recover_check(flag_t *flag) {
    assert(flag != NULL);
    local_time_t now = timer_now_ns();
    // same reason for including 'now < last_raised' as above.
    if (flag->sustained && (now < flag->last_raised || now >= flag->last_raised + FLAG_RECOVER_PERIOD_NS)) {
        flag->sustained = false;
        return true;
    }
    return false;
}

// This is defined as a macro, so that we can directly incorporate the debugf message requested.
macro_define(flag_raisef, f_flag, f_format, f_args...) {
    ({
        if (flag_raise_check(f_flag)) {
            debugf(WARNING, f_format, f_args);
        }
    })
}

macro_define(flag_recoverf, f_flag, f_format, f_args...) {
    ({
        if (flag_recover_check(f_flag)) {
            debugf(WARNING, f_format, f_args);
        }
    })
}

#endif /* FSW_SYNCH_FLAG_H */
