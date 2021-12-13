#ifndef FSW_FAKEWIRE_EXCHANGE_H
#define FSW_FAKEWIRE_EXCHANGE_H

#include <hal/thread.h>
#include <fsw/chart.h>
#include <fsw/fakewire/link.h>

typedef struct fw_exchange_st {
    fw_link_options_t link_opts;

    fw_link_t    io_port;
    fw_encoder_t encoder;
    fw_decoder_t decoder;

    thread_t    exchange_thread;
    semaphore_t exchange_wake;

    chart_t transmit_chart;  // client: exchange_thread, server: link driver, struct io_tx_ent
    chart_t receive_chart;   // client: link driver, server: exchange_thread, struct io_rx_ent
    chart_t *read_chart;     // client: exchange_thread, server: switch task, struct io_rx_ent
    chart_t *write_chart;    // client: switch task, server: exchange_thread, struct io_rx_ent (yes, actually!)
} fw_exchange_t;

// returns 0 if successfully initialized, -1 if an I/O error prevented initialization
int fakewire_exc_init(fw_exchange_t *fwe, fw_link_options_t link_opts, chart_t *read_chart, chart_t *write_chart);

#endif /* FSW_FAKEWIRE_EXCHANGE_H */
