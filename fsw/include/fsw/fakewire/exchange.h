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

    thread_t exchange_thread;

    // input to exchange thread
    queue_t input_queue;
    chart_t transmit_chart;  // client: exchange_thread, server: link driver
    chart_t receive_chart;   // client: link driver, server: exchange_thread
    chart_t *read_chart;     // client: exchange_thread, server: read_cb_thread

    semaphore_t write_ready_sem;
} fw_exchange_t;

// returns 0 if successfully initialized, -1 if an I/O error prevented initialization
int fakewire_exc_init(fw_exchange_t *fwe, fw_link_options_t link_opts, chart_t *read_chart);

void fakewire_exc_notify_chart(fw_exchange_t *fwe);
void fakewire_exc_write(fw_exchange_t *fwe, uint8_t *packet_in, size_t packet_len);

#endif /* FSW_FAKEWIRE_EXCHANGE_H */
