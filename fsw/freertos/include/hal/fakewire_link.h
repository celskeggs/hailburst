#ifndef FSW_FREERTOS_HAL_FAKEWIRE_LINK_H
#define FSW_FREERTOS_HAL_FAKEWIRE_LINK_H

#include <rtos/virtio.h>
#include <hal/thread.h>
#include <fsw/fakewire/codec.h>
#include <fsw/ringbuf.h>

typedef struct {
    mutex_t                     port_mutex;
    SemaphoreHandle_t           port_acquired;
    struct virtio_console_port *port;

    const char *label;

    ringbuf_t     enc_ring;
    fw_encoder_t  encoder;
    fw_decoder_t  decoder;

    thread_t output_thread;
    thread_t input_thread;
} fw_link_t;

#endif /* FSW_FREERTOS_HAL_FAKEWIRE_LINK_H */
