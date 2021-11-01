#ifndef FSW_FAKEWIRE_EXCHANGE_H
#define FSW_FAKEWIRE_EXCHANGE_H

#include <hal/thread.h>
#include <fsw/chart.h>
#include <fsw/wall.h>
#include <fsw/fakewire/link.h>

typedef struct fw_exchange_st {
    fw_link_options_t link_opts;

    fw_link_t    io_port;
    fw_encoder_t encoder;
    fw_decoder_t decoder;

    thread_t    exchange_thread;
    semaphore_t exchange_wake;

    chart_t transmit_chart;  // client: exchange_thread, server: link driver
    chart_t receive_chart;   // client: link driver, server: exchange_thread
    chart_t *read_chart;     // client: exchange_thread, server: calling task
    wall_t   write_wall;     // clients: calling tasks, server: exchange_thread
} fw_exchange_t;

// returns 0 if successfully initialized, -1 if an I/O error prevented initialization
int fakewire_exc_init(fw_exchange_t *fwe, fw_link_options_t link_opts, chart_t *read_chart);

// calls hole_init on the specified hole
void fakewire_exc_attach_writer(fw_exchange_t *fwe, hole_t *hole, size_t hole_size,
                                void (*notify_client)(void *), void *param);

#endif /* FSW_FAKEWIRE_EXCHANGE_H */
