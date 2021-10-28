#ifndef FSW_LINUX_HAL_FAKEWIRE_LINK_H
#define FSW_LINUX_HAL_FAKEWIRE_LINK_H

#include <stdbool.h>

#include <hal/thread.h>
#include <fsw/fakewire/codec.h>

typedef struct {
    int fd_in;
    int fd_out;

    chart_t *rx_chart;

    const char *label;
    semaphore_t input_wake;
    thread_t    input_thread;
} fw_link_t;

#endif /* FSW_LINUX_HAL_FAKEWIRE_LINK_H */
