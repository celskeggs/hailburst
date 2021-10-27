#ifndef FSW_FREERTOS_HAL_FAKEWIRE_LINK_H
#define FSW_FREERTOS_HAL_FAKEWIRE_LINK_H

#include <rtos/virtio.h>
#include <hal/thread.h>
#include <fsw/fakewire/codec.h>

typedef struct {
    chart_t               data_rx;
    chart_t               data_tx;
    struct virtio_console console;
    const char           *label;
    semaphore_t           tx_wake;
    semaphore_t           rx_wake;
    thread_t              rx_thread;

    fw_link_cb_t recv;
    void        *param;
} fw_link_t;

#endif /* FSW_FREERTOS_HAL_FAKEWIRE_LINK_H */
