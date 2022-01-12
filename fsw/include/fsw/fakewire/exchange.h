#ifndef FSW_FAKEWIRE_EXCHANGE_H
#define FSW_FAKEWIRE_EXCHANGE_H

#include <hal/thread.h>
#include <fsw/chart.h>
#include <fsw/fakewire/link.h>

typedef struct fw_exchange_st {
    const char *label;

    fw_encoder_t encoder;
    fw_decoder_t decoder;

    semaphore_t exchange_wake;

    chart_t *transmit_chart; // client: exchange_thread, server: link driver, struct io_tx_ent
    chart_t *receive_chart;  // client: link driver, server: exchange_thread, struct io_rx_ent
    chart_t *read_chart;     // client: exchange_thread, server: switch task, struct io_rx_ent
    chart_t *write_chart;    // client: switch task, server: exchange_thread, struct io_rx_ent (yes, actually!)
} fw_exchange_t;

void fakewire_exc_notify(void *opaque);
void fakewire_exc_init_internal(fw_exchange_t *fwe);
void fakewire_exc_exchange_loop(void *fwe_opaque);

#define FAKEWIRE_EXCHANGE_REGISTER(e_ident, e_link_options, e_read_chart, e_write_chart) \
    extern fw_exchange_t e_ident;                                                        \
    CHART_CLIENT_NOTIFY(e_read_chart, fakewire_exc_notify, &e_ident);                    \
    CHART_SERVER_NOTIFY(e_write_chart, fakewire_exc_notify, &e_ident);                   \
    CHART_REGISTER(e_ident ## _transmit_chart, 1024, 16);                                \
    CHART_CLIENT_NOTIFY(e_ident ## _transmit_chart, fakewire_exc_notify, &e_ident);      \
    CHART_REGISTER(e_ident ## _receive_chart, 1024, 16);                                 \
    CHART_SERVER_NOTIFY(e_ident ## _receive_chart, fakewire_exc_notify, &e_ident);       \
    FAKEWIRE_LINK_REGISTER(e_ident ## _io_port, e_link_options,                          \
                           e_ident ## _receive_chart, e_ident ## _transmit_chart);       \
    fw_exchange_t e_ident = {                                                            \
        .label = (e_link_options).label,                                                 \
        /* io_port, encoder, decoder, exchange_wake not initialized here */              \
        .transmit_chart = &e_ident ## _transmit_chart,                                   \
        .receive_chart = &e_ident ## _receive_chart,                                     \
        .read_chart = &e_read_chart,                                                     \
        .write_chart = &e_write_chart,                                                   \
    };                                                                                   \
    PROGRAM_INIT_PARAM(STAGE_READY, fakewire_exc_init_internal, e_ident, &e_ident);      \
    TASK_REGISTER(e_ident ## _task, "fw_exc_thread", PRIORITY_SERVERS,                   \
                  fakewire_exc_exchange_loop, &e_ident, RESTARTABLE)

#endif /* FSW_FAKEWIRE_EXCHANGE_H */
