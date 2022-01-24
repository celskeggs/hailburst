#ifndef FSW_HEARTBEAT_H
#define FSW_HEARTBEAT_H

#include <hal/thread.h>

TASK_PROTO(heartbeat_task);

#define HEARTBEAT_SCHEDULE()     \
    TASK_SCHEDULE(heartbeat_task)

#endif /* FSW_HEARTBEAT_H */
