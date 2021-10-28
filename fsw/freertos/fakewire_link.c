#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <rtos/timer.h>
#include <fsw/debug.h>
#include <fsw/fakewire/link.h>

enum {
    FW_LINK_RING_SIZE = 1024,
};

//#define DEBUG

#define debug_puts(str) (debugf("[ fakewire_link] [%s] %s", fwl->label, str))
#define debug_printf(fmt, ...) (debugf("[ fakewire_link] [%s] " fmt, fwl->label, __VA_ARGS__))

void fakewire_link_write(fw_link_t *fwl, uint8_t *bytes_in, size_t bytes_count) {
    assert(fwl != NULL && bytes_in != NULL && bytes_count > 0);

    // write one large chunk to the output port
#ifdef DEBUG
    debug_printf("Writing %zu bytes to VIRTIO port...", bytes_count);
#endif
    while (bytes_count > 0) {
        struct io_tx_ent *entry;
        while ((entry = chart_request_start(&fwl->data_tx)) == NULL) {
            // if something still needs to be acknowledged, acknowledge it.
            // TODO: this suggests that charts are not exactly the right data structure here
            if ((entry = chart_ack_start(&fwl->data_tx)) != NULL) {
                chart_ack_send(&fwl->data_tx, entry);
                // now this entry should be available
                assert(entry == chart_request_start(&fwl->data_tx));
                break;
            }
            // otherwise, wait until something changes
            semaphore_take(&fwl->tx_wake);
        }

        // we have an entry!
        size_t current_count = chart_note_size(&fwl->data_tx) - offsetof(struct io_tx_ent, data);
        if (current_count > bytes_count) {
            current_count = bytes_count;
        }
        entry->actual_length = current_count;
        memcpy(entry->data, bytes_in, current_count);
        chart_request_send(&fwl->data_tx, entry);

        bytes_count -= current_count;
        bytes_in += current_count;
    }
#ifdef DEBUG
    debug_printf("Finished writing data to VIRTIO port.", bytes_count);
#endif
}

static void fakewire_link_notify_device(void *opaque) {
    fw_link_t *fwl = (fw_link_t *) opaque;
    assert(fwl != NULL);
    virtio_console_chart_wakeup(&fwl->console);
}

static void fakewire_link_notify_tx_thread(void *opaque) {
    fw_link_t *fwl = (fw_link_t *) opaque;
    assert(fwl != NULL);
    // we don't care if the semaphore cannot be given; this just means there's already a wakeup enqueued.
    (void) semaphore_give(&fwl->tx_wake);
}

void fakewire_link_notify_rx_chart(fw_link_t *fwl) {
    assert(fwl != NULL);
    virtio_console_chart_wakeup(&fwl->console);
}

int fakewire_link_init(fw_link_t *fwl, fw_link_options_t opts, chart_t *data_rx) {
    assert(fwl != NULL && data_rx != NULL && opts.label != NULL && opts.path != NULL);
    memset(fwl, 0, sizeof(fw_link_t));

    // set up debug info real quick
    fwl->label = opts.label;

    // ignore path
    (void) opts.path;

    // make sure flags are the only settings supported on FreeRTOS
    assert(opts.flags == FW_FLAG_VIRTIO);

    // configure the data structures
    semaphore_init(&fwl->tx_wake);
    chart_init(&fwl->data_tx, 1024, 16, fakewire_link_notify_device, fakewire_link_notify_tx_thread, fwl);

    // initialize serial port
    if (!virtio_console_init(&fwl->console, data_rx, &fwl->data_tx)) {
        debugf("Could not configure VIRTIO serial port.");
        chart_destroy(&fwl->data_tx);
        semaphore_destroy(&fwl->tx_wake);
        return -1;
    }

    return 0;
}
