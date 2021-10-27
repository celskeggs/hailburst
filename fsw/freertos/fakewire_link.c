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
        struct virtio_output_entry *entry;
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
        size_t current_count = chart_note_size(&fwl->data_tx) - offsetof(struct virtio_output_entry, data);
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

static void *fakewire_link_rx_loop(void *opaque) {
    fw_link_t *fwl = (fw_link_t *) opaque;
    assert(fwl != NULL);

    uint64_t drain_start = timer_now_ns();

    for (;;) {
        struct virtio_input_entry *entry = chart_reply_start(&fwl->data_rx);
        if (entry == NULL) {
            // no data yet; sleep and then check again
            semaphore_take(&fwl->rx_wake);
            continue;
        }

#ifdef DEBUG
        debug_printf("Read %zd bytes from VIRTIO serial port.", entry->actual_length);
#endif
        assert(entry->actual_length > 0);
        assert(entry->actual_length <= chart_note_size(&fwl->data_rx) - offsetof(struct virtio_input_entry, data));
        assert(entry->receive_timestamp > 0);

        // drain all bytes immediately received after reset, to avoid synchronization spew
        if (entry->receive_timestamp >= drain_start && entry->receive_timestamp < drain_start + TICK_PERIOD_NS) {
            continue;
        }

        // decode the whole buffer at once
        fakewire_dec_decode(&fwl->decoder, entry->data, entry->actual_length, entry->receive_timestamp);

        // consume the reply
        chart_reply_send(&fwl->data_rx, entry);
    }
}

static void fakewire_link_notify_device(void *opaque) {
    fw_link_t *fwl = (fw_link_t *) opaque;
    assert(fwl != NULL);
    virtio_console_chart_wakeup(&fwl->console);
}

static void fakewire_link_notify_rx_thread(void *opaque) {
    fw_link_t *fwl = (fw_link_t *) opaque;
    assert(fwl != NULL);
    // we don't care if the semaphore cannot be given; this just means there's already a wakeup enqueued.
    (void) semaphore_give(&fwl->rx_wake);
}

static void fakewire_link_notify_tx_thread(void *opaque) {
    fw_link_t *fwl = (fw_link_t *) opaque;
    assert(fwl != NULL);
    // we don't care if the semaphore cannot be given; this just means there's already a wakeup enqueued.
    (void) semaphore_give(&fwl->tx_wake);
}

int fakewire_link_init(fw_link_t *fwl, fw_receiver_t *receiver, fw_link_options_t opts) {
    assert(fwl != NULL && receiver != NULL && opts.label != NULL && opts.path != NULL);
    memset(fwl, 0, sizeof(fw_link_t));

    // set up debug info real quick
    fwl->label = opts.label;

    // ignore path
    (void) opts.path;

    // make sure flags are the only settings supported on FreeRTOS
    assert(opts.flags == FW_FLAG_VIRTIO);

    // configure the data structures
    fakewire_dec_init(&fwl->decoder, receiver);
    semaphore_init(&fwl->rx_wake);
    semaphore_init(&fwl->tx_wake);
    chart_init(&fwl->data_rx, 1024, 16, fakewire_link_notify_rx_thread, fakewire_link_notify_device, fwl);
    chart_init(&fwl->data_tx, 1024, 16, fakewire_link_notify_device, fakewire_link_notify_tx_thread, fwl);

    // initialize serial port
    if (!virtio_console_init(&fwl->console, &fwl->data_rx, &fwl->data_tx)) {
        debugf("Could not configure VIRTIO serial port.");
        chart_destroy(&fwl->data_tx);
        chart_destroy(&fwl->data_rx);
        semaphore_destroy(&fwl->tx_wake);
        semaphore_destroy(&fwl->rx_wake);
        return -1;
    }

    // now let's start the receiver thread
    thread_create(&fwl->rx_thread, "fw_rx_loop", PRIORITY_SERVERS, fakewire_link_rx_loop, fwl, NOT_RESTARTABLE);

    return 0;
}
