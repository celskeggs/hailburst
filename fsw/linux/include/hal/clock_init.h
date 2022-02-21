#ifndef FSW_CLOCK_INIT_H
#define FSW_CLOCK_INIT_H

#include <bus/rmap.h>
#include <bus/switch.h>
#include <flight/telemetry.h>

#define CLOCK_EXISTS

typedef struct {
    rmap_t *rmap;
} clock_device_t;

void clock_start_main(clock_device_t *clock);

#define CLOCK_REGISTER(c_ident, c_address, c_switch_in, c_switch_out, c_switch_port)                                  \
    extern clock_device_t c_ident;                                                                                    \
    TASK_REGISTER(c_ident ## _task, clock_start_main, &c_ident, NOT_RESTARTABLE);                                     \
    RMAP_ON_SWITCHES(c_ident ## _rmap, "clock", c_switch_in, c_switch_out, c_switch_port, c_address,                  \
                     sizeof(uint64_t), 0);                                                                            \
    clock_device_t c_ident = {                                                                                        \
        .rmap = &c_ident ## _rmap,                                                                                    \
    }

#define CLOCK_SCHEDULE(c_ident)                                                                                       \
    TASK_SCHEDULE(c_ident ## _task, 100)

// one RMAP channel
#define CLOCK_MAX_IO_FLOW       RMAP_MAX_IO_FLOW

// largest packet size that the switch needs to be able to route
#define CLOCK_MAX_IO_PACKET                                                                                           \
    RMAP_MAX_IO_PACKET(sizeof(uint64_t), 0)

void clock_wait_for_calibration(void);

extern const thread_t clock_cal_notify_task;

#define CLOCK_DEPEND_ON_CALIBRATION(c_client_task) \
    const thread_t clock_cal_notify_task = &c_client_task

#endif /* FSW_CLOCK_INIT_H */
