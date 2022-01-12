#include <rtos/virtio.h>
#include <fsw/debug.h>
#include <fsw/fakewire/link.h>

enum {
    FAKEWIRE_REGION = 31, /* fakewire serial port is attached to VIRTIO MMIO region 31 */
};

static bool fakewire_link_attached = false;

VIRTIO_CONSOLE_REGISTER(virtio_fakewire_link, FAKEWIRE_REGION);

void fakewire_link_init_internal(fw_link_t *link) {
    assert(link != NULL && link->data_rx != NULL && link->data_tx != NULL);

    assert(link->options.flags == FW_FLAG_VIRTIO); // only option supported on FreeRTOS

    // ensure that this is only called once, because multiple VIRTIO configurations for the same memory would conflict
    assert(!fakewire_link_attached);
    fakewire_link_attached = true;

    // initialize serial port
    virtio_console_init(&virtio_fakewire_link, link->data_rx, link->data_tx);
}
