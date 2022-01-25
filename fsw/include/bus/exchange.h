#ifndef FSW_FAKEWIRE_EXCHANGE_H
#define FSW_FAKEWIRE_EXCHANGE_H

#include <hal/thread.h>
#include <fsw/chart.h>
#include <bus/link.h>

enum {
    EXCHANGE_QUEUE_DEPTH = 16,
};

typedef struct fw_exchange_st {
    const char *label;

    fw_encoder_t encoder;
    fw_decoder_t decoder;

    thread_t exchange_task;

    chart_t *transmit_chart; // client: exchange_thread, server: link driver, struct io_tx_ent
    chart_t *receive_chart;  // client: link driver, server: exchange_thread, struct io_rx_ent
    chart_t *read_chart;     // client: exchange_thread, server: switch task, struct io_rx_ent
    chart_t *write_chart;    // client: switch task, server: exchange_thread, struct io_rx_ent (yes, actually!)
} fw_exchange_t;

void fakewire_exc_notify(fw_exchange_t *fwe);
void fakewire_exc_init_internal(fw_exchange_t *fwe);
void fakewire_exc_exchange_loop(fw_exchange_t *fwe);

#define FAKEWIRE_EXCHANGE_REGISTER(e_ident, e_link_options, e_read_chart, e_write_chart)                 \
    extern fw_exchange_t e_ident;                                                                        \
    TASK_REGISTER(e_ident ## _task, "fw_exc_thread", fakewire_exc_exchange_loop, &e_ident, RESTARTABLE); \
    CHART_CLIENT_NOTIFY(e_read_chart, task_rouse, &e_ident ## _task);                                    \
    CHART_SERVER_NOTIFY(e_write_chart, task_rouse, &e_ident ## _task);                                   \
    CHART_REGISTER(e_ident ## _transmit_chart, 1024, EXCHANGE_QUEUE_DEPTH);                              \
    CHART_CLIENT_NOTIFY(e_ident ## _transmit_chart, task_rouse, &e_ident ## _task);                      \
    CHART_REGISTER(e_ident ## _receive_chart, 1024, EXCHANGE_QUEUE_DEPTH);                               \
    CHART_SERVER_NOTIFY(e_ident ## _receive_chart, task_rouse, &e_ident ## _task);                       \
    FAKEWIRE_LINK_REGISTER(e_ident ## _io_port, e_link_options,                                          \
                           e_ident ## _receive_chart, e_ident ## _transmit_chart,                        \
                           EXCHANGE_QUEUE_DEPTH, EXCHANGE_QUEUE_DEPTH);                                  \
    fw_exchange_t e_ident = {                                                                            \
        .label = (e_link_options).label,                                                                 \
        /* io_port, encoder, decoder not initialized here */                                             \
        .exchange_task = &e_ident ## _task,                                                              \
        .transmit_chart = &e_ident ## _transmit_chart,                                                   \
        .receive_chart = &e_ident ## _receive_chart,                                                     \
        .read_chart = &e_read_chart,                                                                     \
        .write_chart = &e_write_chart,                                                                   \
    };                                                                                                   \
    PROGRAM_INIT_PARAM(STAGE_READY, fakewire_exc_init_internal, e_ident, &e_ident)

#define FAKEWIRE_EXCHANGE_SCHEDULE(e_ident)  \
    TASK_SCHEDULE(e_ident ## _task, 100)     \
    FAKEWIRE_LINK_SCHEDULE(e_ident ## _io_port) \
    TASK_SCHEDULE(e_ident ## _task, 100)

#endif /* FSW_FAKEWIRE_EXCHANGE_H */
