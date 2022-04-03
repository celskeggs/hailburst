#ifndef FSW_FREERTOS_HAL_SYSTEM_H
#define FSW_FREERTOS_HAL_SYSTEM_H

#include <rtos/scrubber.h>
#include <hal/thread.h>
#include <hal/clip.h>

CLIP_PROTO(watchdog_voter);
CLIP_PROTO(watchdog_monitor);
TASK_PROTO(scrubber_1_task);
TASK_PROTO(scrubber_2_task);

macro_define(SYSTEM_MAINTENANCE_SCHEDULE) {
    CLIP_SCHEDULE(watchdog_voter,   10)
    CLIP_SCHEDULE(watchdog_monitor, 10)
    TASK_SCHEDULE(scrubber_1_task, 100)
    TASK_SCHEDULE(scrubber_2_task, 100)
}

#endif /* FSW_FREERTOS_HAL_SYSTEM_H */
