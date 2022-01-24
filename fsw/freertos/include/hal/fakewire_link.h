#ifndef FSW_FREERTOS_HAL_FAKEWIRE_LINK_H
#define FSW_FREERTOS_HAL_FAKEWIRE_LINK_H

#include <rtos/virtio.h>
#include <hal/thread.h>
#include <fsw/fakewire/codec.h>

enum {
    FAKEWIRE_LINK_REGION = 31, /* fakewire serial port is attached to VIRTIO MMIO region 31 */
};

void fakewire_link_init_check(const fw_link_options_t *options);

#define FAKEWIRE_LINK_REGISTER(l_ident, l_options, l_rx, l_tx, l_rx_num, l_tx_num)                          \
    PROGRAM_INIT_PARAM(STAGE_RAW, fakewire_link_init_check, l_ident, &(l_options));                         \
    VIRTIO_CONSOLE_REGISTER(l_ident ## _port, FAKEWIRE_LINK_REGION, l_rx, l_tx, l_rx_num, l_tx_num)

#define FAKEWIRE_LINK_SCHEDULE(l_ident) \
    VIRTIO_CONSOLE_SCHEDULE(l_ident ## _port)

#endif /* FSW_FREERTOS_HAL_FAKEWIRE_LINK_H */
