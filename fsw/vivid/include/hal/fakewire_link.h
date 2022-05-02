#ifndef FSW_VIVID_HAL_FAKEWIRE_LINK_H
#define FSW_VIVID_HAL_FAKEWIRE_LINK_H

#include <rtos/virtio_console.h>
#include <hal/clip.h>
#include <bus/codec.h>

#define FAKEWIRE_LINK_RECEIVE_REPLICAS  VIRTIO_INPUT_QUEUE_REPLICAS
#define FAKEWIRE_LINK_TRANSMIT_REPLICAS VIRTIO_OUTPUT_QUEUE_REPLICAS

enum {
    FAKEWIRE_LINK_REGION = 31, /* fakewire serial port is attached to VIRTIO MMIO region 31 */
};

void fakewire_link_init_check(const fw_link_options_t *options);

macro_define(FAKEWIRE_LINK_REGISTER,
             l_ident, l_options, l_rx, l_tx, l_buf_size) {
    PROGRAM_INIT_PARAM(STAGE_RAW, fakewire_link_init_check, l_ident, &(l_options));
    VIRTIO_CONSOLE_REGISTER(symbol_join(l_ident, port), FAKEWIRE_LINK_REGION, l_rx, l_tx, l_buf_size, l_buf_size)
}

macro_define(FAKEWIRE_LINK_SCHEDULE_TRANSMIT, l_ident) {
    VIRTIO_CONSOLE_SCHEDULE_TRANSMIT(symbol_join(l_ident, port))
}

macro_define(FAKEWIRE_LINK_SCHEDULE_RECEIVE, l_ident) {
    VIRTIO_CONSOLE_SCHEDULE_RECEIVE(symbol_join(l_ident, port))
}

#endif /* FSW_VIVID_HAL_FAKEWIRE_LINK_H */
