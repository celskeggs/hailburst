#ifndef FSW_VIVID_HAL_CLIP_H
#define FSW_VIVID_HAL_CLIP_H

// clips are defined as part of the regular scheduler
#include <hal/thread.h>

macro_define(CLIP_PROTO, c_ident) {
    TASK_PROTO(c_ident)
}

static inline bool clip_is_restart(void) {
    return task_get_current()->mut->needs_start;
}

static inline uint32_t clip_remaining_ns(void) {
    return (uint32_t) (schedule_last - timer_now_ns());
}

// asserts if the current task is not executing within a clip
static inline void clip_assert(void) {
    // always a clip on Vivid
}

#endif /* FSW_VIVID_HAL_CLIP_H */
