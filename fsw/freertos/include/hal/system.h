#ifndef FSW_FREERTOS_HAL_SYSTEM_H
#define FSW_FREERTOS_HAL_SYSTEM_H

#include <rtos/scrubber.h>
#include <hal/thread.h>

TASK_PROTO(task_restart_task);
TASK_PROTO(watchdog_task);
TASK_PROTO(scrubber_1_task);
TASK_PROTO(scrubber_2_task);

#define SYSTEM_MAINTENANCE_SCHEDULE() \
    TASK_SCHEDULE(task_restart_task)  \
    TASK_SCHEDULE(watchdog_task)      \
    TASK_SCHEDULE(scrubber_1_task)    \
    TASK_SCHEDULE(scrubber_2_task)

#endif /* FSW_FREERTOS_HAL_SYSTEM_H */
