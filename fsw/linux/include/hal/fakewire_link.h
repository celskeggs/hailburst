#ifndef FSW_LINUX_HAL_FAKEWIRE_LINK_H
#define FSW_LINUX_HAL_FAKEWIRE_LINK_H

#include <stdbool.h>

#include <hal/thread.h>
#include <fsw/fakewire/codec.h>

typedef struct {
    int fd_in;
    int fd_out;

    const char *label;
    thread_t    input_thread;

    fw_link_cb_t recv;
    void        *param;
} fw_link_t;

#endif /* FSW_LINUX_HAL_FAKEWIRE_LINK_H */
