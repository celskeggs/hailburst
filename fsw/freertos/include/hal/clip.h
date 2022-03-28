#ifndef FSW_FREERTOS_FSW_CLIP_H
#define FSW_FREERTOS_FSW_CLIP_H

// clips are defined as part of the regular scheduler
#include <hal/thread.h>

macro_define(CLIP_PROTO, c_ident) {
    TASK_PROTO(c_ident)
}

static inline bool clip_is_restart(void) {
    return task_get_current()->mut->needs_start;
}

// asserts if the current task is not executing within a clip
static inline void clip_assert(void) {
    assertf(task_get_current()->restartable == RESTART_ON_RESCHEDULE,
            "running in task %s, which is not a clip", task_get_name(task_get_current()));
}

#endif /* FSW_FREERTOS_FSW_CLIP_H */
