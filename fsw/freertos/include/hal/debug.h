#ifndef FSW_FREERTOS_FSW_DEBUG_H
#define FSW_FREERTOS_FSW_DEBUG_H

#include <stdlib.h>

#include <hal/loglevel.h>
// for clock_timestamp() needed by debugf macro
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

#define _assert_raw(x,fmt,...) do { \
    if (!(x)) {                     \
        debugf_stable(CRITICAL, Assertion, fmt, ## __VA_ARGS__); \
        abort(); \
    } \
} while (0)

#define assertf(x,fmt,...) _assert_raw(x, "ASSERT: " fmt, __VA_ARGS__)
#define assert(x)          _assert_raw(x, "ASSERT")

#define abortf(fmt,...) do { \
    debugf_stable(CRITICAL, Assertion, "ABORT: " fmt, ## __VA_ARGS__); \
    abort(); \
} while (0)

// restart the current task
extern void restart_current_task(void) __attribute__((noreturn));

#define restartf(fmt,...) do { \
    debugf(WARNING, "RESTART: " fmt, ## __VA_ARGS__); \
    restart_current_task(); \
} while (0)
#define restart() do { \
    debugf(WARNING, "RESTART"); \
    restart_current_task(); \
} while (0)

#define static_assert _Static_assert

#endif /* FSW_FREERTOS_FSW_DEBUG_H */
