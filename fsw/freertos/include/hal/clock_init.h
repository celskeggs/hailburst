#ifndef FSW_CLOCK_INIT_H
#define FSW_CLOCK_INIT_H

#include <hal/thread.h>

TASK_PROTO(clock_start_task);

// clock calibration is unnecessary on FreeRTOS
#define CLOCK_REGISTER(c_ident, c_address, c_switch_in, c_switch_out, c_switch_port)

// no task needed on FreeRTOS
#define CLOCK_SCHEDULE(c_ident)

// no RMAP channels for clock on FreeRTOS
#define CLOCK_MAX_IO_FLOW   0

// largest packet size that the switch needs to be able to route
#define CLOCK_MAX_IO_PACKET 0

static inline void clock_wait_for_calibration(void) {
    /* do nothing; no calibration is required on FreeRTOS */
}

#define CLOCK_DEPEND_ON_CALIBRATION(c_client_task)

#endif /* FSW_CLOCK_INIT_H */
