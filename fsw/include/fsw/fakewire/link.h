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

// returns 0 if successfully initialized, -1 if an I/O error prevented initialization
int fakewire_link_init(fw_link_t *fwl, fw_receiver_t *receiver, fw_link_options_t opts);

void fakewire_link_send_data(fw_link_t *fwl, uint8_t *bytes_in, size_t bytes_count);
void fakewire_link_send_ctrl(fw_link_t *fwl, fw_ctrl_t symbol, uint32_t param);

#endif /* FSW_FAKEWIRE_LINK_H */
