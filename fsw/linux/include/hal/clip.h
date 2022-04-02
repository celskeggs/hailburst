#ifndef FSW_LINUX_FSW_CLIP_H
#define FSW_LINUX_FSW_CLIP_H

#include <hal/debug.h>
#include <hal/thread.h>

typedef struct {
    const char *label;
    void (*clip_play)(void *);
    void *clip_argument;
    bool clip_just_started;
} clip_t;

void clip_loop(clip_t *clip);

macro_define(CLIP_PROTO, c_ident) {
    extern clip_t c_ident;
    TASK_PROTO(symbol_join(c_ident, task))
}

macro_define(CLIP_REGISTER, c_ident, c_play, c_arg) {
    clip_t c_ident = {
        .label = symbol_str(c_ident),
        .clip_play = blame_caller { PP_ERASE_TYPE(c_play, c_arg) },
        .clip_argument = (void *) (c_arg),
        .clip_just_started = true,
    };
    TASK_REGISTER(symbol_join(c_ident, task), clip_loop, &c_ident, RESTARTABLE)
}

macro_define(CLIP_SCHEDULE, c_ident, c_micros) {
    TASK_SCHEDULE(symbol_join(c_ident, task), c_micros)
}

// returns true if the clip has just been restarted, or started for this first time
static inline bool clip_is_restart(void) {
    thread_t task = task_get_current();
    assert(task->start_routine == PP_ERASE_TYPE(clip_loop, (clip_t *) NULL));
    clip_t *clip = (clip_t *) task->start_parameter;
    return clip->clip_just_started;
}

// asserts if the current task is not executing within a clip
static inline void clip_assert(void) {
    assertf(task_get_current()->start_routine == PP_ERASE_TYPE(clip_loop, (clip_t *) NULL),
            "running in task %s, which is not a clip", task_get_name(task_get_current()));
}

#endif /* FSW_LINUX_FSW_CLIP_H */
