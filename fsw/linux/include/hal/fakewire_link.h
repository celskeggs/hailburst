#ifndef FSW_LINUX_HAL_FAKEWIRE_LINK_H
#define FSW_LINUX_HAL_FAKEWIRE_LINK_H

#include <stdbool.h>

#include <hal/thread.h>
#include <fsw/fakewire/codec.h>

typedef struct {
    int fd_in;
    int fd_out;

    chart_t *rx_chart;
    chart_t *tx_chart;

    fw_link_options_t options;

    thread_t    configure_thread;
    semaphore_t fds_ready;

    const char *label;
    semaphore_t receive_wake;
    thread_t    receive_thread;
    semaphore_t transmit_wake;
    thread_t    transmit_thread;
} fw_link_t;

#endif /* FSW_LINUX_HAL_FAKEWIRE_LINK_H */
