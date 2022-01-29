#ifndef FSW_CLOCK_INIT_H
#define FSW_CLOCK_INIT_H

#include <hal/thread.h>

TASK_PROTO(clock_start_task);

// clock calibration is unnecessary on FreeRTOS
#define CLOCK_REGISTER(c_ident, c_address, c_rx, c_tx)

// no task needed on FreeRTOS
#define CLOCK_SCHEDULE(c_ident)

static inline void clock_wait_for_calibration(void) {
    /* do nothing; no calibration is required on FreeRTOS */
}

#define CLOCK_DEPEND_ON_CALIBRATION(c_client_task)

#endif /* FSW_CLOCK_INIT_H */
