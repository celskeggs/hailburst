#ifndef FSW_LINUX_HAL_FAKEWIRE_LINK_H
#define FSW_LINUX_HAL_FAKEWIRE_LINK_H

#include <stdbool.h>

#include <hal/thread.h>
#include <bus/codec.h>

typedef struct {
    int fd_in;
    int fd_out;

    chart_t *rx_chart;
    chart_t *tx_chart;

    fw_link_options_t options;

    thread_t receive_task;
    thread_t transmit_task;
} fw_link_t;

void fakewire_link_rx_loop(fw_link_t *fwl);
void fakewire_link_tx_loop(fw_link_t *fwl);
void fakewire_link_configure(fw_link_t *fwl);

// l_rx_num and l_tx_num are only used on FreeRTOS
macro_define(FAKEWIRE_LINK_REGISTER,
             l_ident, l_options, l_rx, l_tx, l_rx_num, l_tx_num) {
    extern fw_link_t l_ident;
    TASK_REGISTER(symbol_join(l_ident, cfg), fakewire_link_configure, &l_ident, NOT_RESTARTABLE);
    TASK_REGISTER(symbol_join(l_ident, rxl), fakewire_link_rx_loop, &l_ident, NOT_RESTARTABLE);
    TASK_REGISTER(symbol_join(l_ident, txl), fakewire_link_tx_loop, &l_ident, NOT_RESTARTABLE);
    fw_link_t l_ident = {
        .fd_in = -1,
        .fd_out = -1,
        .rx_chart = &(l_rx),
        .tx_chart = &(l_tx),
        .options = (l_options),
        .receive_task = &(symbol_join(l_ident, rxl)),
        .transmit_task = &(symbol_join(l_ident, txl)),
    };
    CHART_CLIENT_NOTIFY(l_rx, task_rouse, &symbol_join(l_ident, rxl));
    CHART_SERVER_NOTIFY(l_tx, task_rouse, &symbol_join(l_ident, txl))
}

macro_define(FAKEWIRE_LINK_SCHEDULE, l_ident) {
    TASK_SCHEDULE(symbol_join(l_ident, cfg), 100)
    TASK_SCHEDULE(symbol_join(l_ident, rxl), 100)
    TASK_SCHEDULE(symbol_join(l_ident, txl), 100)
}

#endif /* FSW_LINUX_HAL_FAKEWIRE_LINK_H */
