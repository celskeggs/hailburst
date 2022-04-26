#ifndef FSW_VIVID_HAL_DEBUG_H
#define FSW_VIVID_HAL_DEBUG_H

#include <stdlib.h>

#include <hal/loglevel.h>
// for clock_timestamp_fast() needed by debugf macro
#include <flight/clock.h>

#ifndef __PYTHON_PREPROCESS__

#warning Need python preprocessor phase to deal with debugf substitution
extern void debugf_core(loglevel_t level, const char *stable_id, const char *format, ...);

#endif

#define TIMEFMT "%u.%09u"
#define TIMEARG(x) (uint32_t) ((x) / CLOCK_NS_PER_SEC), (uint32_t) ((x) % CLOCK_NS_PER_SEC)

#define debugf(level, fmt, ...)                   debugf_core(level, "",         fmt, ## __VA_ARGS__)
#define debugf_stable(level, stable_id, fmt, ...) debugf_core(level, #stable_id, fmt, ## __VA_ARGS__)

// invocations to debugf_internal are injected by the clang AST rewriter plugin
extern void debugf_internal(const void **data_sequences, const size_t *data_sizes, size_t data_num);

struct debugf_metadata {
    uint32_t loglevel;
    const char *stable_id;
    const char *format;
    const char *filename;
    uint32_t line_number;
} __attribute__((packed));

// restart the current task
extern void restart_current_task(void) __attribute__((noreturn));

macro_define(assert, x) {
    ({
        if (!(x)) {
            blame_caller { debugf_stable(WARNING, Assertion, "ASSERT"); }
            restart_current_task();
        }
    })
}

macro_define(assertf, x, fmt, args...) {
    ({
        if (!(x)) {
            blame_caller { debugf_stable(WARNING, Assertion, "ASSERT: " fmt, args); }
            restart_current_task();
        }
    })
}

macro_define(abortf, fmt, args...) {
    ({
        blame_caller { debugf_stable(CRITICAL, Assertion, "ABORT: " fmt, args); }
        abort();
    })
}

macro_define(restartf, fmt, args...) {
    ({
        blame_caller { debugf(WARNING, "RESTART: " fmt, args); }
        restart_current_task();
    })
}

macro_define(restart) {
    ({
        blame_caller { debugf(WARNING, "RESTART"); }
        restart_current_task();
    })
}

#define static_assert _Static_assert

#endif /* FSW_VIVID_HAL_DEBUG_H */
