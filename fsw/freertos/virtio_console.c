#include <endian.h>
#include <stddef.h>
#include <stdint.h>

#include <rtos/gic.h>
#include <rtos/virtio.h>
#include <hal/clock.h>
#include <hal/debug.h>
#include <hal/thread.h>

enum {
    VIRTIO_FAKEWIRE_PORT_INDEX  = 1,

    VIRTIO_CONSOLE_VQ_RECEIVE  = 0,
    VIRTIO_CONSOLE_VQ_TRANSMIT = 1,

    VIRTIO_CONSOLE_VQ_CTRL_BASE = 2,

    REPLICA_ID = 0,
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

static void virtio_console_send_ctrl_msg(duct_txn_t *txn, uint32_t id, uint16_t event, uint16_t value) {
    struct virtio_console_control ctrl = {
        .id    = id,
        .event = event,
        .value = value,
    };
    // should never run out of spaces; we only ever send three, and there are four slots!
    duct_send_message(txn, &ctrl, sizeof(ctrl), 0);
}

static inline uint32_t virtio_console_port_to_queue_index(uint32_t port) {
    port *= 2;
    if (port >= VIRTIO_CONSOLE_VQ_CTRL_BASE) {
        port += 2;
    }
    return port;
}

void virtio_console_control_clip(struct virtio_console *console) {
    assert(console != NULL);

    duct_txn_t recv_txn;
    duct_txn_t send_txn;

    duct_receive_prepare(&recv_txn, console->control_rx, REPLICA_ID);
    duct_send_prepare(&send_txn, console->control_tx, REPLICA_ID);

    if (!console->sent_initial) {
        // request initialization
        virtio_console_send_ctrl_msg(&send_txn, 0xFFFFFFFF, VIRTIO_CONSOLE_DEVICE_READY, 1);
        console->sent_initial = true;
    }

    struct {
        struct virtio_console_control recv;
        uint8_t extra[VIRTIO_CONSOLE_CTRL_RECV_MARGIN];
    } raw;
    assert(duct_message_size(console->control_rx) == sizeof(raw));
    size_t length;
    while ((length = duct_receive_message(&recv_txn, &raw, NULL)) > 0) {
        debugf(DEBUG, "Received CONTROL message on queue: id=%u, event=%u, value=%u (chain_bytes=%u)",
               raw.recv.id, raw.recv.event, raw.recv.value, length);

        if (raw.recv.event == VIRTIO_CONSOLE_DEVICE_ADD) {
            assert(length == sizeof(struct virtio_console_control));

            if (raw.recv.id != VIRTIO_FAKEWIRE_PORT_INDEX) {
                debugf(CRITICAL, "WARNING: Did not expect to find serial port %u attached to anything.", raw.recv.id);
            } else if (console->confirmed_port_present) {
                debugf(CRITICAL, "WARNING: Did not expect to receive duplicate message about port %u.", raw.recv.id);
            } else {
                debugf(DEBUG, "Discovered serial port %u as expected for fakewire connection.", raw.recv.id);
                console->confirmed_port_present = true;

                // send messages to allow the serial port to receive data.
                virtio_console_send_ctrl_msg(&send_txn, VIRTIO_FAKEWIRE_PORT_INDEX, VIRTIO_CONSOLE_PORT_READY, 1);
                virtio_console_send_ctrl_msg(&send_txn, VIRTIO_FAKEWIRE_PORT_INDEX, VIRTIO_CONSOLE_PORT_OPEN, 1);
            }
        } else if (raw.recv.event == VIRTIO_CONSOLE_PORT_NAME) {
            assert(length >= sizeof(struct virtio_console_control));
            // nothing to do
        } else if (raw.recv.event == VIRTIO_CONSOLE_PORT_OPEN) {
            assert(length == sizeof(struct virtio_console_control));
            assert(raw.recv.id == VIRTIO_FAKEWIRE_PORT_INDEX);
            assert(raw.recv.value == 1);
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
            debugf(DEBUG, "Serial port %u confirmed open for fakewire connection; re-notifying queue %u.",
                   raw.recv.id, console->data_receive_queue->queue_index);
            virtio_device_force_notify_queue(console->data_receive_queue);
        } else {
            debugf(CRITICAL, "Unhandled console control event: %u.", raw.recv.event);
        }
    }

    duct_receive_commit(&recv_txn);
    duct_send_commit(&send_txn);
}

void virtio_console_configure_internal(struct virtio_console *console) {
    struct virtio_console_config *config =
            (struct virtio_console_config *) virtio_device_config_space(console->devptr);

    debugf(DEBUG, "Maximum number of ports supported by VIRTIO console device: %d", config->max_nr_ports);
    // TODO: should I really be treating 'num_queues' as public?
    assert(console->devptr->num_queues <= (config->max_nr_ports + 1) * 2);
}
