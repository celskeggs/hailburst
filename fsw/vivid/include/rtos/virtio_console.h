#ifndef FSW_VIVID_RTOS_VIRTIO_CONSOLE_H
#define FSW_VIVID_RTOS_VIRTIO_CONSOLE_H

#include <rtos/virtio.h>

enum {
    // VIRTIO type for a console device
    VIRTIO_CONSOLE_ID = 3,

    // max handled length of received console names
    VIRTIO_CONSOLE_CTRL_RECV_MARGIN = 32,
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
    CLIP_SCHEDULE(symbol_join(v_ident, clip), 15)
    VIRTIO_DEVICE_QUEUE_SCHEDULE(symbol_join(v_ident, device), 3, 10) /* control.tx */
    VIRTIO_DEVICE_QUEUE_SCHEDULE(symbol_join(v_ident, device), 5, 50) /* data[1].tx */
}

macro_define(VIRTIO_CONSOLE_SCHEDULE_RECEIVE, v_ident) {
    VIRTIO_DEVICE_QUEUE_SCHEDULE(symbol_join(v_ident, device), 2, 10) /* control.rx */
    VIRTIO_DEVICE_QUEUE_SCHEDULE(symbol_join(v_ident, device), 4, 20) /* data[1].rx */
}

#endif /* FSW_VIVID_RTOS_VIRTIO_CONSOLE_H */