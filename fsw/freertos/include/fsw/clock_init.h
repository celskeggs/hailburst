#ifndef FSW_CLOCK_INIT_H
#define FSW_CLOCK_INIT_H

// clock calibration is unnecessary on FreeRTOS
#define CLOCK_REGISTER(c_ident, c_address, c_rx, c_tx)

static inline void clock_wait_for_calibration(void) {
    /* do nothing; no calibration is required on FreeRTOS */
}

#define CLOCK_DEPEND_ON_CALIBRATION(c_client_task)

#endif /* FSW_CLOCK_INIT_H */
