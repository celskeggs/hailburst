#ifndef FSW_VIVID_HAL_SYSTEM_H
#define FSW_VIVID_HAL_SYSTEM_H

#include <rtos/scrubber.h>
#include <hal/thread.h>
#include <hal/clip.h>

macro_define(SYSTEM_MAINTENANCE_REGISTER) {
    SCRUBBER_REGISTER()
}

macro_define(SYSTEM_MAINTENANCE_SCHEDULE) {
    SCRUBBER_SCHEDULE()
}

macro_define(SYSTEM_MAINTENANCE_WATCH) {
    SCRUBBER_WATCH()
}

#endif /* FSW_VIVID_HAL_SYSTEM_H */
