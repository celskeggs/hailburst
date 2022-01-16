#ifndef FSW_CLOCK_INIT_H
#define FSW_CLOCK_INIT_H

#include <fsw/telemetry.h>
#include <fsw/fakewire/rmap.h>
#include <fsw/fakewire/switch.h>

#define CLOCK_EXISTS

typedef struct {
    rmap_t               *rmap;
    const rmap_addr_t     address;
} clock_device_t;

void clock_start_main(clock_device_t *clock);

#define CLOCK_REGISTER(c_ident, c_address, c_rx, c_tx)                                                          \
    extern clock_device_t c_ident;                                                                              \
    TASK_REGISTER(c_ident ## _task, "clock-start", clock_start_main, &c_ident, NOT_RESTARTABLE);                \
    RMAP_REGISTER(c_ident ## _rmap, sizeof(uint64_t), 0, c_rx, c_tx, c_ident ## _task);                         \
    clock_device_t c_ident = {                                                                                  \
        .rmap = &c_ident ## _rmap,                                                                              \
        .address = (c_address),                                                                                 \
    }

void clock_wait_for_calibration(void);

extern const thread_t clock_cal_notify_task;

#define CLOCK_DEPEND_ON_CALIBRATION(c_client_task) \
    const thread_t clock_cal_notify_task = &c_client_task

#endif /* FSW_CLOCK_INIT_H */
