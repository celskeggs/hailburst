#include <string.h>
#include <unistd.h>

#include <rtos/timer.h>
#include <fsw/debug.h>
#include <fsw/fakewire/link.h>

int fakewire_link_init(fw_link_t *fwl, fw_link_options_t opts, chart_t *data_rx, chart_t *data_tx) {
    assert(fwl != NULL && data_rx != NULL && opts.label != NULL && opts.path != NULL);
    memset(fwl, 0, sizeof(fw_link_t));

    // set up debug info real quick
    fwl->label = opts.label;

    // ignore path
    (void) opts.path;

    // make sure flags are the only settings supported on FreeRTOS
    assert(opts.flags == FW_FLAG_VIRTIO);

    // initialize serial port
    if (!virtio_console_init(&fwl->console, data_rx, data_tx)) {
        debugf("Could not configure VIRTIO serial port.");
        return -1;
    }

    return 0;
}
