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

    semaphore_t *receive_wake;
    semaphore_t *transmit_wake;
    semaphore_t *fds_ready;
} fw_link_t;

void fakewire_link_rx_loop(void *opaque);
void fakewire_link_tx_loop(void *opaque);
void fakewire_link_notify_rx_chart(void *opaque);
void fakewire_link_notify_tx_chart(void *opaque);
void fakewire_link_configure(void *opaque);

#define FAKEWIRE_LINK_REGISTER(l_ident, l_options, l_rx, l_tx)                                                        \
    SEMAPHORE_REGISTER(l_ident ## _rxs);                                                                              \
    SEMAPHORE_REGISTER(l_ident ## _txs);                                                                              \
    SEMAPHORE_REGISTER(l_ident ## _frs);                                                                              \
    fw_link_t l_ident = {                                                                                             \
        .fd_in = -1,                                                                                                  \
        .fd_out = -1,                                                                                                 \
        .rx_chart = &(l_rx),                                                                                          \
        .tx_chart = &(l_tx),                                                                                          \
        .options = (l_options),                                                                                       \
        .receive_wake = &(l_ident ## _rxs),                                                                           \
        .transmit_wake = &(l_ident ## _txs),                                                                          \
        .fds_ready = &(l_ident ## _frs),                                                                              \
    };                                                                                                                \
    CHART_CLIENT_NOTIFY(l_rx, fakewire_link_notify_rx_chart, &l_ident);                                               \
    CHART_SERVER_NOTIFY(l_tx, fakewire_link_notify_tx_chart, &l_ident);                                               \
    TASK_REGISTER(l_ident ## _cfg, "fw_config",  PRIORITY_INIT,  fakewire_link_configure, &l_ident, NOT_RESTARTABLE); \
    TASK_REGISTER(l_ident ## _rxl, "fw_rx_loop", PRIORITY_SERVERS, fakewire_link_rx_loop, &l_ident, NOT_RESTARTABLE); \
    TASK_REGISTER(l_ident ## _txl, "fw_tx_loop", PRIORITY_SERVERS, fakewire_link_tx_loop, &l_ident, NOT_RESTARTABLE)

#endif /* FSW_LINUX_HAL_FAKEWIRE_LINK_H */
