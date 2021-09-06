#ifndef FSW_FAKEWIRE_LINK_H
#define FSW_FAKEWIRE_LINK_H

#include <hal/fakewire_link.h>

enum {
    FW_FLAG_SERIAL    = 0,
    FW_FLAG_VIRTIO    = 1,
    FW_FLAG_FIFO_PROD = 2,
    FW_FLAG_FIFO_CONS = 3,
};

// returns 0 if successfully initialized, -1 if an I/O error prevented initialization
int fakewire_link_init(fw_link_t *fwl, fw_receiver_t *receiver, const char *path, int flags, const char *label);
// safely shuts down any currently-running communication without freeing memory
void fakewire_link_shutdown(fw_link_t *fwl);
// tears down object; must not be called while any transmissions are in progress.
void fakewire_link_destroy(fw_link_t *fwl);
fw_receiver_t *fakewire_link_interface(fw_link_t *fwl);

#endif /* FSW_FAKEWIRE_LINK_H */
