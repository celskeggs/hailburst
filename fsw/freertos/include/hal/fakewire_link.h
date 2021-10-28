#ifndef FSW_FREERTOS_HAL_FAKEWIRE_LINK_H
#define FSW_FREERTOS_HAL_FAKEWIRE_LINK_H

#include <rtos/virtio.h>
#include <hal/thread.h>
#include <fsw/fakewire/codec.h>

typedef struct {
    chart_t               data_tx;
    struct virtio_console console;
    const char           *label;
    semaphore_t           tx_wake;
} fw_link_t;

#endif /* FSW_FREERTOS_HAL_FAKEWIRE_LINK_H */
