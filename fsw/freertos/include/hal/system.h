#ifndef FSW_FREERTOS_HAL_SYSTEM_H
#define FSW_FREERTOS_HAL_SYSTEM_H

#include <rtos/scrubber.h>
#include <hal/thread.h>
#include <hal/clip.h>

WATCHDOG_ASPECT_PROTO(scrubber_1_aspect);
WATCHDOG_ASPECT_PROTO(scrubber_2_aspect);
CLIP_PROTO(scrubber_1_clip);
CLIP_PROTO(scrubber_2_clip);

macro_define(SYSTEM_MAINTENANCE_SCHEDULE) {
    SCRUBBER_SCHEDULE(scrubber_1)
    SCRUBBER_SCHEDULE(scrubber_2)
}

macro_define(SYSTEM_MAINTENANCE_WATCH) {
    SCRUBBER_WATCH(scrubber_1)
    SCRUBBER_WATCH(scrubber_2)
}

#endif /* FSW_FREERTOS_HAL_SYSTEM_H */
