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
#define FAKEWIRE_LINK_REGISTER(l_ident, l_options, l_rx, l_tx, l_rx_num, l_tx_num)                                    \
    extern fw_link_t l_ident;                                                                                         \
    TASK_REGISTER(l_ident ## _cfg, "fw_config",  fakewire_link_configure, &l_ident, NOT_RESTARTABLE);                 \
    TASK_REGISTER(l_ident ## _rxl, "fw_rx_loop", fakewire_link_rx_loop, &l_ident, NOT_RESTARTABLE);                   \
    TASK_REGISTER(l_ident ## _txl, "fw_tx_loop", fakewire_link_tx_loop, &l_ident, NOT_RESTARTABLE);                   \
    fw_link_t l_ident = {                                                                                             \
        .fd_in = -1,                                                                                                  \
        .fd_out = -1,                                                                                                 \
        .rx_chart = &(l_rx),                                                                                          \
        .tx_chart = &(l_tx),                                                                                          \
        .options = (l_options),                                                                                       \
        .receive_task = &(l_ident ## _rxl),                                                                           \
        .transmit_task = &(l_ident ## _txl),                                                                          \
    };                                                                                                                \
    CHART_CLIENT_NOTIFY(l_rx, task_rouse, &l_ident ## _rxl);                                                          \
    CHART_SERVER_NOTIFY(l_tx, task_rouse, &l_ident ## _txl)

#define FAKEWIRE_LINK_SCHEDULE(l_ident)                                                                               \
    TASK_SCHEDULE(l_ident ## _cfg, 100)                                                                               \
    TASK_SCHEDULE(l_ident ## _rxl, 100)                                                                               \
    TASK_SCHEDULE(l_ident ## _txl, 100)

#endif /* FSW_LINUX_HAL_FAKEWIRE_LINK_H */
