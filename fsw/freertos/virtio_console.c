#include <assert.h>
#include <endian.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <rtos/gic.h>
#include <rtos/virtio.h>
#include <rtos/virtqueue.h>
#include <hal/thread.h>
#include <fsw/clock.h>
#include <fsw/debug.h>

// #define DEBUG_INIT
// #define DEBUG_VIRTQ

enum {
    VIRTIO_MMIO_ADDRESS_BASE   = 0x0A000000,
    VIRTIO_MMIO_ADDRESS_STRIDE = 0x200,
    VIRTIO_MMIO_IRQS_BASE      = IRQ_SPI_BASE + 16,
    VIRTIO_MMIO_REGION_NUM     = 32,

    VIRTIO_MMIO_FAKEWIRE_REGION = 31,
    VIRTIO_FAKEWIRE_PORT_INDEX  = 1,

    VIRTIO_MMIO_FAKEWIRE_ADDRESS = VIRTIO_MMIO_ADDRESS_BASE + VIRTIO_MMIO_ADDRESS_STRIDE * VIRTIO_MMIO_FAKEWIRE_REGION,
    VIRTIO_MMIO_FAKEWIRE_IRQ     = VIRTIO_MMIO_IRQS_BASE + VIRTIO_MMIO_FAKEWIRE_REGION,

    VIRTIO_CONSOLE_ID = 3,

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

enum {
    // max handled length of received console names
    VIRTIO_CONSOLE_CTRL_RECV_MARGIN = 32,
};

struct virtio_console_config {
    uint16_t cols;
    uint16_t rows;
    uint32_t max_nr_ports;
    uint32_t emerg_wr;
};
static_assert(sizeof(struct virtio_console_config) == 12, "wrong sizeof(struct virtio_console_config)");

struct virtio_console_control {
    uint32_t id;    /* Port number */
    uint16_t event; /* The kind of control event */
    uint16_t value; /* Extra information for the event */
};
static_assert(sizeof(struct virtio_console_control) == 8, "wrong sizeof(struct virtio_console_control)");

static bool virtio_console_feature_select(uint64_t *features) {
    // check feature bits
    if (!(*features & VIRTIO_F_VERSION_1)) {
        printf("VIRTIO device featureset (0x%016x) does not include VIRTIO_F_VERSION_1 (0x%016x).\n"
               "Legacy devices are not supported.\n", *features, VIRTIO_F_VERSION_1);
        return false;
    }
    if (!(*features & VIRTIO_CONSOLE_F_MULTIPORT)) {
        printf("VIRTIO device featureset (0x%016x) does not include VIRTIO_CONSOLE_F_MULTIPORT (0x%016x).\n"
               "This configuration is not yet supported.\n", *features, VIRTIO_CONSOLE_F_MULTIPORT);
        return false;
    }

    // select just those two features
    *features = VIRTIO_F_VERSION_1 | VIRTIO_CONSOLE_F_MULTIPORT;
    return true;
}

static bool virtio_fakewire_attached = false;

// for our end of the data charts only
void virtio_console_chart_wakeup(struct virtio_console *console) {
    assert(console->initialized);

    virtio_device_chart_wakeup(&console->device);
}

static void virtio_console_control_chart_console_wakeup(void *opaque) {
    struct virtio_console *console = (struct virtio_console *) opaque;
    assert(console != NULL && console->initialized);
    // we ignore the case where we fail to give the semaphore... that just means another wake request is already on the
    // queue, and therefore there's no need for us to enqueue another wakeup!
    (void) semaphore_give(&console->control_wake);
}

static void virtio_console_control_chart_device_wakeup(void *opaque) {
    struct virtio_console *console = (struct virtio_console *) opaque;
    assert(console != NULL && console->initialized);
    virtio_device_chart_wakeup(&console->device);
}

static void virtio_console_send_ctrl_msg(struct virtio_console *console, uint32_t id, uint16_t event, uint16_t value) {
    struct virtio_output_entry *entry = chart_request_start(&console->control_tx);
    // should never run out of spaces; we only ever send three, and there are four slots!
    assert(entry != NULL);
    entry->actual_length = sizeof(struct virtio_console_control);
    *(struct virtio_console_control *) entry->data = (struct virtio_console_control) {
        .id =    id,
        .event = event,
        .value = value,
    };
    chart_request_send(&console->control_tx, entry);
}

static void *virtio_console_control_loop(void *opaque) {
    struct virtio_console *console = (struct virtio_console *) opaque;
    assert(console != NULL && console->initialized);

    // request initialization
    virtio_console_send_ctrl_msg(console, 0xFFFFFFFF, VIRTIO_CONSOLE_DEVICE_READY, 1);

    for (;;) {
        // perform any required acknowledgements for the transmit queue
        struct virtio_output_entry *tx_ack_entry = chart_ack_start(&console->control_tx);
        if (tx_ack_entry != NULL) {
#ifdef DEBUG_INIT
            printf("Completed transmit of VIRTIO CONSOLE control message.\n");
#endif
            chart_ack_send(&console->control_tx, tx_ack_entry);
        }

        // receive any requests on the receive queue
        struct virtio_input_entry *rx_entry = chart_reply_start(&console->control_rx);
        if (rx_entry != NULL) {
            struct virtio_console_control *recv = (struct virtio_console_control *) rx_entry->data;

#ifdef DEBUG_INIT
            printf("Received CONTROL message on queue: id=%u, event=%u, value=%u (chain_bytes=%u)\n",
                   recv->id, recv->event, recv->value, rx_entry->actual_length);
#endif

            if (recv->event == VIRTIO_CONSOLE_DEVICE_ADD) {
                assert(rx_entry->actual_length == sizeof(struct virtio_console_control));

                if (recv->id != VIRTIO_FAKEWIRE_PORT_INDEX) {
                    printf("WARNING: Did not expect to find serial port %u attached to anything.\n", recv->id);
                } else if (console->confirmed_port_present) {
                    printf("WARNING: Did not expect to receive duplicate message about fakewire port %u.\n", recv->id);
                } else {
#ifdef DEBUG_INIT
                    printf("Discovered serial port %u as expected for fakewire connection.\n", recv->id);
#endif
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
                assert(recv->value == 1);
                // nothing to do
            } else {
                printf("UNHANDLED event: ctrl event %u\n", recv->event);
            }

            chart_reply_send(&console->control_rx, recv);
        }

        if (tx_ack_entry == NULL && rx_entry == NULL) {
            semaphore_take(&console->control_wake);
        }
    }
}

static inline uint32_t virtio_console_port_to_queue_index(uint32_t port) {
    port *= 2;
    if (port >= VIRTIO_CONSOLE_VQ_CTRL_BASE) {
        port += 2;
    }
    return port;
}

bool virtio_console_init(struct virtio_console *console, chart_t *data_rx, chart_t *data_tx) {
    assert(!virtio_fakewire_attached);
    virtio_fakewire_attached = true;

    assert(!console->initialized);

    console->confirmed_port_present = false;

    if (!virtio_device_init(&console->device, VIRTIO_MMIO_FAKEWIRE_ADDRESS, VIRTIO_MMIO_FAKEWIRE_IRQ,
                            VIRTIO_CONSOLE_ID, virtio_console_feature_select)) {
        return false;
    }

    struct virtio_console_config *config =
            (struct virtio_console_config *) virtio_device_config_space(&console->device);

#ifdef DEBUG_INIT
    printf("Maximum number of ports supported by VIRTIO device: %d\n", config->max_nr_ports);
#endif

    // TODO: should I really be treating 'num_queues' as public?
    assert(console->device.num_queues == (config->max_nr_ports + 1) * 2);

    chart_init(&console->control_rx, sizeof(struct virtio_console_control) + sizeof(struct virtio_input_entry), 4,
               virtio_console_control_chart_console_wakeup, virtio_console_control_chart_device_wakeup, console);

    if (!virtio_device_setup_queue(&console->device,
                VIRTIO_CONSOLE_VQ_CTRL_BASE + VIRTIO_CONSOLE_VQ_RECEIVE, QUEUE_INPUT, &console->control_rx)) {
        virtio_device_fail(&console->device);
        chart_destroy(&console->control_rx);
        return false;
    }

    chart_init(&console->control_tx, sizeof(struct virtio_console_control) + sizeof(struct virtio_output_entry), 4,
               virtio_console_control_chart_device_wakeup, virtio_console_control_chart_console_wakeup, console);

    if (!virtio_device_setup_queue(&console->device,
                VIRTIO_CONSOLE_VQ_CTRL_BASE + VIRTIO_CONSOLE_VQ_TRANSMIT, QUEUE_OUTPUT, &console->control_tx)) {
        virtio_device_fail(&console->device);
        chart_destroy(&console->control_rx);
        chart_destroy(&console->control_tx);
        return false;
    }

    size_t base_queue = virtio_console_port_to_queue_index(VIRTIO_FAKEWIRE_PORT_INDEX);

    if (!virtio_device_setup_queue(&console->device, base_queue + VIRTIO_CONSOLE_VQ_RECEIVE, QUEUE_INPUT, data_rx)) {
        virtio_device_fail(&console->device);
        chart_destroy(&console->control_rx);
        chart_destroy(&console->control_tx);
        return false;
    }

    if (!virtio_device_setup_queue(&console->device, base_queue + VIRTIO_CONSOLE_VQ_TRANSMIT, QUEUE_OUTPUT, data_tx)) {
        virtio_device_fail(&console->device);
        chart_destroy(&console->control_rx);
        chart_destroy(&console->control_tx);
        return false;
    }

    semaphore_init(&console->control_wake);

    console->initialized = true;
    virtio_device_start(&console->device);

    // start the task that talks on the control queues
    thread_create(&console->control_task, "serial-ctrl", PRIORITY_INIT, virtio_console_control_loop, console,
                  NOT_RESTARTABLE);

    return true;
}
