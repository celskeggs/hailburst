#ifndef FSW_FREERTOS_RTOS_VIRTIO_H
#define FSW_FREERTOS_RTOS_VIRTIO_H

#include <stdint.h>
#include <stdbool.h>

#include <FreeRTOS.h>
#include <task.h>

#include <rtos/gic.h>
#include <rtos/virtqueue.h>
#include <hal/thread.h>
#include <hal/init.h>
#include <synch/duct.h>

enum {
    // configuration for the particular VIRTIO MMIO layout of the qemu-system-arm -M virt simulation board
    VIRTIO_MMIO_ADDRESS_BASE   = 0x0A000000,
    VIRTIO_MMIO_ADDRESS_STRIDE = 0x200,
    VIRTIO_MMIO_IRQS_BASE      = IRQ_SPI_BASE + 16,
    VIRTIO_MMIO_REGION_NUM     = 32,

    // VIRTIO type for a console device
    VIRTIO_CONSOLE_ID = 3,

    // max handled length of received console names
    VIRTIO_CONSOLE_CTRL_RECV_MARGIN = 32,
};

typedef enum {
    QUEUE_INPUT  = 1,
    QUEUE_OUTPUT = 2,
} virtio_queue_dir_t;

// If a queue is an INPUT queue (i.e. it reads from the device),
//     then virtio is the duct SENDER and the other end is the duct RECEIVER.
// If a queue is an OUTPUT queue (i.e. it writes to the device),
//     then virtio is the duct RECEIVER and the other end is the duct SENDER.
struct virtio_device_queue {
    struct virtio_device *parent_device;
    uint32_t              queue_index;

    duct_t            *duct;
    uint8_t           *buffer; // size is the same as the duct max flow * duct message size
    virtio_queue_dir_t direction;

    size_t   queue_num;
    uint16_t last_used_idx;

    struct virtq_desc  *desc;
    struct virtq_avail *avail;
    struct virtq_used  *used;
};

// read the features, write back selected features, or abort/assert if features are not acceptable
typedef void (*virtio_feature_select_cb)(uint64_t *features);

struct virtio_device {
    bool initialized;

    struct virtio_mmio_registers *mmio;
    virtio_feature_select_cb      feature_select_cb;

    uint32_t irq;
    uint32_t expected_device_id;

    uint32_t num_queues;
};

struct virtio_console {
    struct virtio_device *devptr;
    bool sent_initial;

    struct virtio_device_queue *data_receive_queue;

    duct_t *control_rx;
    duct_t *control_tx;

    bool confirmed_port_present;
};

struct virtio_console_control {
    uint32_t id;    /* Port number */
    uint16_t event; /* The kind of control event */
    uint16_t value; /* Extra information for the event */
};
static_assert(sizeof(struct virtio_console_control) == 8, "wrong sizeof(struct virtio_console_control)");

void virtio_device_init_internal(struct virtio_device *device);
void virtio_device_start_internal(struct virtio_device *device);
void virtio_monitor_clip(struct virtio_device *device);

macro_define(VIRTIO_DEVICE_REGISTER,
             v_ident, v_region_id, v_device_id, v_feature_select) {
    struct virtio_device v_ident = {
        .initialized = false,
        .mmio = (struct virtio_mmio_registers *)
                    (VIRTIO_MMIO_ADDRESS_BASE + VIRTIO_MMIO_ADDRESS_STRIDE * (v_region_id)),
        .feature_select_cb = (v_feature_select),
        .irq = VIRTIO_MMIO_IRQS_BASE + (v_region_id),
        .expected_device_id = (v_device_id),
        .num_queues = 0, /* to be populated */
    };
    PROGRAM_INIT_PARAM(STAGE_RAW, virtio_device_init_internal, v_ident, &v_ident);
    PROGRAM_INIT_PARAM(STAGE_READY, virtio_device_start_internal, v_ident, &v_ident)
}

// this may only be called before the scheduler starts
void virtio_device_setup_queue_internal(struct virtio_device_queue *queue);
void virtio_queue_monitor_clip(struct virtio_device_queue *queue);

macro_define(VIRTIO_DEVICE_QUEUE_REGISTER,
             v_ident, v_queue_index, v_direction, v_duct, v_duct_flow, v_queue_flow, v_duct_capacity) {
    static uint8_t symbol_join(v_ident, v_queue_index, buffer)[(v_queue_flow) * (v_duct_capacity)];
    static struct virtq_desc symbol_join(v_ident, v_queue_index, desc)[v_queue_flow] __attribute__((__aligned__(16)));
    static struct {
        /* weird init syntax required due to flexible array member */
        struct virtq_avail avail;
        uint16_t flex_ring[v_queue_flow];
    } symbol_join(v_ident, v_queue_index, avail) __attribute__((__aligned__(2))) = {
        .avail = {
            .flags = 0,
            .idx   = (v_direction) == QUEUE_INPUT ? (v_queue_flow) : 0,
        },
        // populate all of the avail ring entries to point to their corresponding descriptors.
        // we won't need to change these again.
        .flex_ring = {
            static_repeat(v_queue_flow, desc_idx) {
                desc_idx,
            }
        },
    };
    static struct {
        /* weird init syntax required due to flexible array member */
        struct virtq_used used;
        struct virtq_used_elem ring[v_queue_flow];
    } symbol_join(v_ident, v_queue_index, used) __attribute__((__aligned__(4)));
    struct virtio_device_queue symbol_join(v_ident, v_queue_index, queue) = {
        .parent_device = &v_ident,
        .queue_index = (v_queue_index),
        .duct = &(v_duct),
        .buffer = symbol_join(v_ident, v_queue_index, buffer),
        .direction = (v_direction),
        .queue_num = (v_queue_flow),
        .last_used_idx = 0,
        .desc = symbol_join(v_ident, v_queue_index, desc),
        .avail = &symbol_join(v_ident, v_queue_index, avail).avail,
        .used = &symbol_join(v_ident, v_queue_index, used).used,
    };
    static void symbol_join(v_ident, v_queue_index, init)(void) {
        assert(v_ident.initialized == true);
        assert((v_queue_index) < v_ident.num_queues);
        if ((v_direction) == QUEUE_INPUT) {
            assert((v_duct_flow) <= (v_queue_flow));
        } else {
            assert((v_duct_flow) == (v_queue_flow));
        }
        assert(duct_max_flow(&v_duct) == (v_duct_flow));
        assert(duct_message_size(&v_duct) == (v_duct_capacity));
        virtio_device_setup_queue_internal(&symbol_join(v_ident, v_queue_index, queue));
    }
    PROGRAM_INIT(STAGE_READY, symbol_join(v_ident, v_queue_index, init));
    CLIP_REGISTER(symbol_join(v_ident, v_queue_index, monitor_clip), virtio_queue_monitor_clip,
                  &symbol_join(v_ident, v_queue_index, queue));
}

macro_define(VIRTIO_DEVICE_QUEUE_REF, v_ident, v_queue_index) {
    (&symbol_join(v_ident, v_queue_index, queue))
}

macro_define(VIRTIO_DEVICE_QUEUE_SCHEDULE, v_ident, v_queue_index, v_nanos) {
    CLIP_SCHEDULE(symbol_join(v_ident, v_queue_index, monitor_clip), v_nanos)
}

void virtio_console_feature_select(uint64_t *features);
void virtio_console_control_clip(struct virtio_console *console);
void virtio_console_configure_internal(struct virtio_console *console);

#define VIRTIO_CONSOLE_CRX_SIZE (sizeof(struct virtio_console_control) + VIRTIO_CONSOLE_CTRL_RECV_MARGIN)
#define VIRTIO_CONSOLE_CRX_FLOW    4
#define VIRTIO_CONSOLE_CTX_SIZE (sizeof(struct virtio_console_control))
#define VIRTIO_CONSOLE_CTX_FLOW    4

macro_define(VIRTIO_CONSOLE_REGISTER,
             v_ident, v_region_id, v_data_rx, v_data_tx, v_rx_capacity, v_tx_capacity) {
    VIRTIO_DEVICE_REGISTER(symbol_join(v_ident, device), v_region_id, VIRTIO_CONSOLE_ID,
                           virtio_console_feature_select);
    DUCT_REGISTER(symbol_join(v_ident, crx), 1, 1,
                  VIRTIO_CONSOLE_CRX_FLOW, VIRTIO_CONSOLE_CRX_SIZE, DUCT_RECEIVER_FIRST);
    DUCT_REGISTER(symbol_join(v_ident, ctx), 1, 1,
                  VIRTIO_CONSOLE_CTX_FLOW, VIRTIO_CONSOLE_CTX_SIZE, DUCT_SENDER_FIRST);
    VIRTIO_DEVICE_QUEUE_REGISTER(symbol_join(v_ident, device), 2, /* control.rx */
                                 QUEUE_INPUT,  symbol_join(v_ident, crx),
                                 VIRTIO_CONSOLE_CRX_FLOW, VIRTIO_CONSOLE_CRX_FLOW, VIRTIO_CONSOLE_CRX_SIZE);
    VIRTIO_DEVICE_QUEUE_REGISTER(symbol_join(v_ident, device), 3, /* control.tx */
                                 QUEUE_OUTPUT, symbol_join(v_ident, ctx),
                                 VIRTIO_CONSOLE_CTX_FLOW, VIRTIO_CONSOLE_CTX_FLOW, VIRTIO_CONSOLE_CTX_SIZE);
    // merge is enabled for the input queue, because duct streams should be single-element, but it is possible that
    // data received is split across multiple buffers by the virtio device, even if it doesn't fill them.
    VIRTIO_DEVICE_QUEUE_REGISTER(symbol_join(v_ident, device), 4, /* data[1].rx */
                                 QUEUE_INPUT,  v_data_rx, 1, 3, v_rx_capacity);
    VIRTIO_DEVICE_QUEUE_REGISTER(symbol_join(v_ident, device), 5, /* data[1].tx */
                                 QUEUE_OUTPUT, v_data_tx, 1, 1, v_tx_capacity);
    struct virtio_console v_ident = {
        .devptr = &symbol_join(v_ident, device),
        .sent_initial = false,
        .data_receive_queue = VIRTIO_DEVICE_QUEUE_REF(symbol_join(v_ident, device), 4), /* data[1].rx */
        .control_rx = &symbol_join(v_ident, crx),
        .control_tx = &symbol_join(v_ident, ctx),
        .confirmed_port_present = false,
    };
    CLIP_REGISTER(symbol_join(v_ident, clip), virtio_console_control_clip, &v_ident);
    PROGRAM_INIT_PARAM(STAGE_CRAFT, virtio_console_configure_internal, v_ident, &v_ident)
}

// We have to schedule serial-ctrl before the virtio monitor, because while it isn't needed
// during regular execution, it is on the critical path for activating the spacecraft bus.
// The very first message it sends MUST go out immediately!
macro_define(VIRTIO_CONSOLE_SCHEDULE_TRANSMIT, v_ident) {
    CLIP_SCHEDULE(symbol_join(v_ident, clip), 7)
    VIRTIO_DEVICE_QUEUE_SCHEDULE(symbol_join(v_ident, device), 3, 5) /* control.tx */
    VIRTIO_DEVICE_QUEUE_SCHEDULE(symbol_join(v_ident, device), 5, 5) /* data[1].tx */
}

macro_define(VIRTIO_CONSOLE_SCHEDULE_RECEIVE, v_ident) {
    VIRTIO_DEVICE_QUEUE_SCHEDULE(symbol_join(v_ident, device), 2, 5) /* control.rx */
    VIRTIO_DEVICE_QUEUE_SCHEDULE(symbol_join(v_ident, device), 4, 7) /* data[1].rx */
}

void *virtio_device_config_space(struct virtio_device *device);

// for a queue already set up using virtio_device_setup_queue, this function spuriously notifies the queue.
void virtio_device_force_notify_queue(struct virtio_device_queue *queue);

#endif /* FSW_FREERTOS_RTOS_VIRTIO_H */
