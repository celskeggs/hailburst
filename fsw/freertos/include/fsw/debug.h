#ifndef FSW_FREERTOS_FSW_DEBUG_H
#define FSW_FREERTOS_FSW_DEBUG_H

#include <stdlib.h>

// for timer_now_ns() needed by debugf macro
#include <rtos/timer_min.h>

#ifndef __PYTHON_PREPROCESS__

#warning Need python preprocessor phase to deal with debugf substitution
extern void debugf(const char *format, ...);

#endif

// invocations to debugf_internal are injected by the clang AST rewriter plugin
extern void debugf_internal(const void **data_sequences, const size_t *data_sizes, size_t data_num);

struct debugf_metadata {
    const char *format;
    const char *filename;
    uint32_t line_number;
} __attribute__((packed));

#define _assert_raw(x,fmt,...) do { \
    if (!(x)) {                     \
        debugf(fmt, ## __VA_ARGS__); \
        abort(); \
    } \
} while (0)

#define assertf(x,fmt,...) _assert_raw(x, "ASSERT: " fmt, __VA_ARGS__)
#define assert(x)          _assert_raw(x, "ASSERT")

#define static_assert _Static_assert


#endif /* FSW_FREERTOS_FSW_DEBUG_H */
