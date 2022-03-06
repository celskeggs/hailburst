#ifndef FSW_LINUX_HAL_FAKEWIRE_LINK_H
#define FSW_LINUX_HAL_FAKEWIRE_LINK_H

#include <stdbool.h>

#include <hal/thread.h>
#include <synch/duct.h>
#include <bus/codec.h>

typedef struct {
    int fd_in;
    int fd_out;

    size_t buffer_size;
    uint8_t *rx_buffer;
    uint8_t *tx_buffer;

    duct_t *rx_duct;
    duct_t *tx_duct;

    fw_link_options_t options;
} fw_link_t;

void fakewire_link_rx_clip(fw_link_t *fwl);
void fakewire_link_tx_clip(fw_link_t *fwl);
void fakewire_link_configure(fw_link_t *fwl);

macro_define(FAKEWIRE_LINK_REGISTER,
             l_ident, l_options, l_rx, l_tx, l_buf_size) {
    extern fw_link_t l_ident;
    TASK_REGISTER(symbol_join(l_ident, cfg), fakewire_link_configure, &l_ident, NOT_RESTARTABLE);
    CLIP_REGISTER(symbol_join(l_ident, rxc), fakewire_link_rx_clip, &l_ident);
    CLIP_REGISTER(symbol_join(l_ident, txc), fakewire_link_tx_clip, &l_ident);
    uint8_t symbol_join(l_ident, rx_buffer)[l_buf_size];
    uint8_t symbol_join(l_ident, tx_buffer)[l_buf_size];
    fw_link_t l_ident = {
        .fd_in = -1,
        .fd_out = -1,
        .buffer_size = (l_buf_size),
        .rx_buffer = symbol_join(l_ident, rx_buffer),
        .tx_buffer = symbol_join(l_ident, tx_buffer),
        .rx_duct = &(l_rx),
        .tx_duct = &(l_tx),
        .options = (l_options),
    }
}

macro_define(FAKEWIRE_LINK_SCHEDULE_TRANSMIT, l_ident) {
    TASK_SCHEDULE(symbol_join(l_ident, cfg), 100)
    CLIP_SCHEDULE(symbol_join(l_ident, txc), 100)
}

macro_define(FAKEWIRE_LINK_SCHEDULE_RECEIVE, l_ident) {
    CLIP_SCHEDULE(symbol_join(l_ident, rxc), 100)
}

#endif /* FSW_LINUX_HAL_FAKEWIRE_LINK_H */
