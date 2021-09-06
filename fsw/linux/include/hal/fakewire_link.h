#ifndef FSW_LINUX_HAL_FAKEWIRE_LINK_H
#define FSW_LINUX_HAL_FAKEWIRE_LINK_H

#include <stdbool.h>

#include <hal/thread.h>
#include <fsw/fakewire/codec.h>
#include <fsw/ringbuf.h>

typedef struct {
    int fd_in;
    int fd_out;

    const char *label;
    bool shutdown;

    fw_receiver_t interface;
    ringbuf_t     enc_ring;
    fw_encoder_t  encoder;
    fw_decoder_t  decoder;

    thread_t output_thread;
    thread_t input_thread;
} fw_link_t;

#endif /* FSW_LINUX_HAL_FAKEWIRE_LINK_H */
