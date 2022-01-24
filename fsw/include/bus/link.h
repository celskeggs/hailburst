#ifndef FSW_FAKEWIRE_LINK_H
#define FSW_FAKEWIRE_LINK_H

enum {
    FW_FLAG_SERIAL    = 0,
    FW_FLAG_VIRTIO    = 1,
    FW_FLAG_FIFO_PROD = 2,
    FW_FLAG_FIFO_CONS = 3,
};

typedef struct fw_link_options_st {
    const char *label;
    const char *path;
    int         flags;
} fw_link_options_t;

#include <hal/fakewire_link.h>

#endif /* FSW_FAKEWIRE_LINK_H */
