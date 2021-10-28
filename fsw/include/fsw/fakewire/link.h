#ifndef FSW_FAKEWIRE_LINK_H
#define FSW_FAKEWIRE_LINK_H

#include <hal/fakewire_link.h>

enum {
    FW_FLAG_SERIAL    = 0,
    FW_FLAG_VIRTIO    = 1,
    FW_FLAG_FIFO_PROD = 2,
    FW_FLAG_FIFO_CONS = 3,
};

typedef struct {
    const char *label;
    const char *path;
    int         flags;
} fw_link_options_t;

// initializes both the link AND the encoder specified!
// returns 0 if successfully initialized, -1 if an I/O error prevented initialization
int fakewire_link_init(fw_link_t *fwl, fw_link_options_t opts, chart_t *data_rx, chart_t *data_tx);

#endif /* FSW_FAKEWIRE_LINK_H */
