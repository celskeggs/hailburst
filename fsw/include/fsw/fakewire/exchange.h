#ifndef FSW_FAKEWIRE_EXCHANGE_H
#define FSW_FAKEWIRE_EXCHANGE_H

#include <hal/thread.h>
#include <fsw/chart.h>
#include <fsw/fakewire/link.h>

// sop_timestamp_ns: start-of-packet timestamp (nanoseconds)
typedef void (*fakewire_exc_read_cb)(void *param, uint8_t *packet_data, size_t packet_length, uint64_t sop_timestamp_ns);

typedef struct {
    fw_link_options_t link_options;
    // receive settings
    size_t               recv_max_size;
    fakewire_exc_read_cb recv_callback;
    void                *recv_param;
} fw_exchange_options_t;

typedef struct fw_exchange_st {
    fw_exchange_options_t options;

    fw_link_t         io_port;
    fw_encoder_t      encoder;
    fw_decoder_t      decoder;
    fw_receiver_t     link_interface;

    thread_t exchange_thread;
    thread_t read_cb_thread;
    thread_t transmit_thread;

    // input to exchange thread
    queue_t input_queue;
    chart_t transmit_chart;  // client: exchange_thread, server: transmit_thread
    semaphore_t transmit_wake; // used for client->server wakeups
    queue_t read_cb_queue;
    semaphore_t write_ready_sem;

    uint8_t *recv_buffer; // allocated size: options.recv_max_size
} fw_exchange_t;

// returns 0 if successfully initialized, -1 if an I/O error prevented initialization
int fakewire_exc_init(fw_exchange_t *fwe, fw_exchange_options_t opts);

void fakewire_exc_write(fw_exchange_t *fwe, uint8_t *packet_in, size_t packet_len);

#endif /* FSW_FAKEWIRE_EXCHANGE_H */
