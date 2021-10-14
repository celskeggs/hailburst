#ifndef FSW_FREERTOS_HAL_WATCHDOG_H
#define FSW_FREERTOS_HAL_WATCHDOG_H

typedef enum {
    WATCHDOG_ASPECT_RADIO_UPLINK = 0,
    WATCHDOG_ASPECT_RADIO_DOWNLINK,
    WATCHDOG_ASPECT_TELEMETRY,
    WATCHDOG_ASPECT_HEARTBEAT,

    WATCHDOG_ASPECT_NUM,
} watchdog_aspect_t;

void watchdog_init(void);
void watchdog_ok(watchdog_aspect_t aspect);
void watchdog_force_reset(void) __attribute__((noreturn));

#endif /* FSW_FREERTOS_HAL_WATCHDOG_H */
