#ifndef FSW_LINUX_FSW_CLIP_H
#define FSW_LINUX_FSW_CLIP_H

#include <hal/thread.h>

typedef struct {
    const char *label;
    void (*clip_play)(void *);
    void *clip_argument;
} clip_t;

void clip_loop(clip_t *clip);

macro_define(CLIP_REGISTER, c_ident, c_play, c_arg) {
    clip_t c_ident = {
        .label = symbol_str(c_ident),
        .clip_play = PP_ERASE_TYPE(c_play, c_arg),
        .clip_argument = (void *) (c_arg),
    };
    TASK_REGISTER(symbol_join(c_ident, task), clip_loop, &c_ident, RESTARTABLE)
}

macro_define(CLIP_SCHEDULE, c_ident, c_micros) {
    TASK_SCHEDULE(symbol_join(c_ident, task), c_micros)
}

#endif /* FSW_LINUX_FSW_CLIP_H */
