#ifndef FSW_VIVID_HAL_CLIP_H
#define FSW_VIVID_HAL_CLIP_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include <rtos/config.h>
#include <rtos/replicate.h>
#include <rtos/scheduler.h>

// clip wrapper that runs within the execution context
void clip_play_direct(void (*entrypoint)(void*)) __attribute__((noreturn));
// scheduler call to enter the execution context and run clip_play_direct
void clip_enter_context(void (*entrypoint)(void*)) __attribute__((noreturn));

macro_define(CLIP_REGISTER, c_ident, c_play, c_arg) {
#if ( VIVID_REPLICATE_TASK_CODE == 1 )
    // We need to define a single function that references both clip_enter_context AND the entrypoint so that we can
    // avoid replicating the two separately.
    void symbol_join(c_ident, enter_context_unreplicated)(void) {
        clip_enter_context(PP_ERASE_TYPE(c_play, c_arg));
    }
    REPLICATE_OBJECT_CODE(symbol_join(c_ident, enter_context_unreplicated), symbol_join(c_ident, enter_context));
#else /* VIVID_REPLICATE_TASK_CODE == 0 */
    void symbol_join(c_ident, enter_context)(void) {
        clip_enter_context(PP_ERASE_TYPE(c_play, c_arg));
    }
#endif
    clip_mut_t symbol_join(c_ident, mutable) = {
        .needs_start         = true,
        .hit_restart         = false,
        .recursive_exception = false,
        .clip_running        = false,
        .clip_next_tick      = 0,
    };
    clip_t c_ident = {
        .mut             = &symbol_join(c_ident, mutable),
        .label           = symbol_str(c_ident),
        .enter_context   = symbol_join(c_ident, enter_context),
        .start_arg       = (void *) c_arg,
    }
}

static inline bool clip_is_restart(void) {
    return schedule_get_clip()->mut->needs_start;
}

// asserts if the current task is not executing within a clip
static inline void clip_assert(void) {
    // always a clip on Vivid
}

#endif /* FSW_VIVID_HAL_CLIP_H */
