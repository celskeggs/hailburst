#include <string.h>
#include <unistd.h>

#include <rtos/timer.h>
#include <rtos/virtio.h>
#include <fsw/debug.h>
#include <fsw/fakewire/link.h>

enum {
    FAKEWIRE_REGION = 31, /* fakewire serial port is attached to VIRTIO MMIO region 31 */
};

static bool fakewire_link_attached = false;

VIRTIO_CONSOLE_REGISTER(virtio_fakewire_link, FAKEWIRE_REGION);

void fakewire_link_init(fw_link_t *fwl, fw_link_options_t opts, chart_t *data_rx, chart_t *data_tx) {
    assert(fwl != NULL && data_rx != NULL && opts.label != NULL && opts.path != NULL);
    memset(fwl, 0, sizeof(fw_link_t));

    // set up debug info real quick
    fwl->label = opts.label;

    // ignore path
    (void) opts.path;

    // make sure flags are the only settings supported on FreeRTOS
    assert(opts.flags == FW_FLAG_VIRTIO);

    // ensure that this is only called once, because multiple VIRTIO configurations for the same memory would conflict
    assert(!fakewire_link_attached);
    fakewire_link_attached = true;

    // initialize serial port
    virtio_console_init(&virtio_fakewire_link, data_rx, data_tx);
}
