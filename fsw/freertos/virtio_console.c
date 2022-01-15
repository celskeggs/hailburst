#include <endian.h>
#include <stddef.h>
#include <stdint.h>

#include <rtos/gic.h>
#include <rtos/virtio.h>
#include <hal/thread.h>
#include <fsw/clock.h>
#include <fsw/debug.h>
#include <fsw/io.h>

enum {
    VIRTIO_FAKEWIRE_PORT_INDEX  = 1,

    VIRTIO_CONSOLE_VQ_RECEIVE  = 0,
    VIRTIO_CONSOLE_VQ_TRANSMIT = 1,

    VIRTIO_CONSOLE_VQ_CTRL_BASE = 2,
};

enum {
    VIRTIO_CONSOLE_DEVICE_READY  = 0,
    VIRTIO_CONSOLE_DEVICE_ADD    = 1,
    VIRTIO_CONSOLE_DEVICE_REMOVE = 2,
    VIRTIO_CONSOLE_PORT_READY    = 3,
    VIRTIO_CONSOLE_CONSOLE_PORT  = 4,
    VIRTIO_CONSOLE_RESIZE        = 5,
    VIRTIO_CONSOLE_PORT_OPEN     = 6,
    VIRTIO_CONSOLE_PORT_NAME     = 7,
};

enum {
    VIRTIO_CONSOLE_F_SIZE        = 1ull << 0,
    VIRTIO_CONSOLE_F_MULTIPORT   = 1ull << 1, // **
    VIRTIO_CONSOLE_F_EMERG_WRITE = 1ull << 2, // **

    VIRTIO_F_RING_INDIRECT_DESC = 1ull << 28, // **
    VIRTIO_F_RING_EVENT_IDX     = 1ull << 29, // **
    VIRTIO_F_VERSION_1          = 1ull << 32, // **
    VIRTIO_F_ACCESS_PLATFORM    = 1ull << 33,
    VIRTIO_F_RING_PACKED        = 1ull << 34,
    VIRTIO_F_IN_ORDER           = 1ull << 35,
    VIRTIO_F_ORDER_PLATFORM     = 1ull << 36,
    VIRTIO_F_SR_IOV             = 1ull << 37,
    VIRTIO_F_NOTIFICATION_DATA  = 1ull << 38,
};

struct virtio_console_config {
    uint16_t cols;
    uint16_t rows;
    uint32_t max_nr_ports;
    uint32_t emerg_wr;
};
static_assert(sizeof(struct virtio_console_config) == 12, "wrong sizeof(struct virtio_console_config)");

void virtio_console_feature_select(uint64_t *features) {
    // check feature bits
    if (!(*features & VIRTIO_F_VERSION_1)) {
        abortf("VIRTIO console device featureset (0x%016x) does not include VIRTIO_F_VERSION_1 (0x%016llx). "
               "Legacy devices are not supported.", *features, VIRTIO_F_VERSION_1);
    }
    if (!(*features & VIRTIO_CONSOLE_F_MULTIPORT)) {
        abortf("VIRTIO console device featureset (0x%016x) does not include VIRTIO_CONSOLE_F_MULTIPORT (0x%016llx). "
               "This configuration is not yet supported.", *features, VIRTIO_CONSOLE_F_MULTIPORT);
    }

    // select just those two features
    *features = VIRTIO_F_VERSION_1 | VIRTIO_CONSOLE_F_MULTIPORT;
}

static void virtio_console_send_ctrl_msg(struct virtio_console *console, uint32_t id, uint16_t event, uint16_t value) {
    struct io_tx_ent *entry = chart_request_start(console->control_tx);
    // should never run out of spaces; we only ever send three, and there are four slots!
    assert(entry != NULL);
    entry->actual_length = sizeof(struct virtio_console_control);
    *(struct virtio_console_control *) entry->data = (struct virtio_console_control) {
        .id =    id,
        .event = event,
        .value = value,
    };
    chart_request_send(console->control_tx, 1);
}

static inline uint32_t virtio_console_port_to_queue_index(uint32_t port) {
    port *= 2;
    if (port >= VIRTIO_CONSOLE_VQ_CTRL_BASE) {
        port += 2;
    }
    return port;
}

void virtio_console_control_loop(struct virtio_console *console) {
    assert(console != NULL);

    // request initialization
    virtio_console_send_ctrl_msg(console, 0xFFFFFFFF, VIRTIO_CONSOLE_DEVICE_READY, 1);

    for (;;) {
        // receive any requests on the receive queue
        struct io_rx_ent *rx_entry = chart_reply_start(console->control_rx);
        if (rx_entry == NULL) {
            task_doze();
            continue;
        }

        struct virtio_console_control *recv = (struct virtio_console_control *) rx_entry->data;

        debugf(DEBUG, "Received CONTROL message on queue: id=%u, event=%u, value=%u (chain_bytes=%u)",
               recv->id, recv->event, recv->value, rx_entry->actual_length);

        if (recv->event == VIRTIO_CONSOLE_DEVICE_ADD) {
            assert(rx_entry->actual_length == sizeof(struct virtio_console_control));

            if (recv->id != VIRTIO_FAKEWIRE_PORT_INDEX) {
                debugf(CRITICAL, "WARNING: Did not expect to find serial port %u attached to anything.", recv->id);
            } else if (console->confirmed_port_present) {
                debugf(CRITICAL, "WARNING: Did not expect to receive duplicate message about fakewire port %u.", recv->id);
            } else {
                debugf(DEBUG, "Discovered serial port %u as expected for fakewire connection.", recv->id);
                console->confirmed_port_present = true;

                // send messages to allow the serial port to receive data.
                virtio_console_send_ctrl_msg(console, VIRTIO_FAKEWIRE_PORT_INDEX, VIRTIO_CONSOLE_PORT_READY, 1);
                virtio_console_send_ctrl_msg(console, VIRTIO_FAKEWIRE_PORT_INDEX, VIRTIO_CONSOLE_PORT_OPEN, 1);
            }
        } else if (recv->event == VIRTIO_CONSOLE_PORT_NAME) {
            assert(rx_entry->actual_length >= sizeof(struct virtio_console_control));
            // nothing to do
        } else if (recv->event == VIRTIO_CONSOLE_PORT_OPEN) {
            assert(rx_entry->actual_length == sizeof(struct virtio_console_control));
            assert(recv->id == VIRTIO_FAKEWIRE_PORT_INDEX);
            assert(recv->value == 1);
            // okay... this is a little messy. basically, QEMU's virtio implementation doesn't re-check whether it
            // should request data from the underlying serial port except at a very specific time: when the receive
            // queue has been notified AND it is connected on the guest end.
            // the problem is that we set up the descriptors and the receive queue BEFORE we configure them via the
            // control queue. so the notification to tell the virtio device that there are buffers for it to load
            // data into doesn't actually cause any data to be loaded. and actually opening the port doesn't do
            // anything either.
            // so we need to remind the virtio device that, yes, we DID give it a bunch of descriptors, and could
            // you please load those now, thank you. so I've added virtio_device_force_notify_queue as a "backdoor"
            // function to let us send this seemingly-spurious notification.
            uint32_t queue = virtio_console_port_to_queue_index(recv->id) + VIRTIO_CONSOLE_VQ_RECEIVE;
            debugf(DEBUG, "Serial port %u confirmed open for fakewire connection; re-notifying queue %u.",
                   recv->id, queue);
            virtio_device_force_notify_queue(console->devptr, queue);
        } else {
            debugf(CRITICAL, "Unhandled console control event: %u.", recv->event);
        }

        chart_reply_send(console->control_rx, 1);
    }
}

void virtio_console_configure_internal(struct virtio_console *console) {
    struct virtio_console_config *config =
            (struct virtio_console_config *) virtio_device_config_space(console->devptr);

    debugf(DEBUG, "Maximum number of ports supported by VIRTIO console device: %d", config->max_nr_ports);
    // TODO: should I really be treating 'num_queues' as public?
    assert(console->devptr->num_queues <= (config->max_nr_ports + 1) * 2);
}
