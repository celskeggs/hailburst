#ifndef FSW_FREERTOS_HAL_WATCHDOG_H
#define FSW_FREERTOS_HAL_WATCHDOG_H

#include <stdlib.h>

typedef enum {
    WATCHDOG_ASPECT_RADIO_UPLINK = 0,
    WATCHDOG_ASPECT_RADIO_DOWNLINK,
    WATCHDOG_ASPECT_TELEMETRY,
    WATCHDOG_ASPECT_HEARTBEAT,

    WATCHDOG_ASPECT_NUM,
} watchdog_aspect_t;

// TODO: implement watchdog support on Linux

static inline void watchdog_init(void) {
    // do nothing
}

static inline void watchdog_ok(watchdog_aspect_t aspect) {
    (void) aspect;
    // do nothing
}

static inline void watchdog_force_reset(void) {
    // no support on Linux
    abort();
}

#endif /* FSW_FREERTOS_HAL_WATCHDOG_H */
