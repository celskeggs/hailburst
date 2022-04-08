#ifndef FSW_LINUX_HAL_WATCHDOG_H
#define FSW_LINUX_HAL_WATCHDOG_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct {} watchdog_aspect_t;

// TODO: implement watchdog support on Linux

macro_define(WATCHDOG_ASPECT, a_ident, a_timeout_ns, a_sender_replicas) {
    watchdog_aspect_t a_ident = {}
}

macro_define(WATCHDOG_REGISTER, w_ident, w_aspects) {
    // no support on Linux
}

macro_define(WATCHDOG_SCHEDULE, w_ident) {
    // no support on Linux
}

static inline void watchdog_indicate(watchdog_aspect_t *aspect, uint8_t replica_id, bool ok) {
    // no support on Linux
    (void) aspect;
    (void) replica_id;
    (void) ok;
}

static inline void watchdog_force_reset(void) {
    // no support on Linux
    abort();
}

#endif /* FSW_LINUX_HAL_WATCHDOG_H */
