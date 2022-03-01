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
#include <synch/chart.h>

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
//     then virtio is the chart CLIENT and the other end is the chart SERVER,
//     and the elements of the chart are io_rx_ent structures.
// If a queue is an OUTPUT queue (i.e. it writes to the device),
//     then virtio is the chart SERVER and the other end is the chart CLIENT,
//     and the elements of the chart are io_tx_ent structures.
struct virtio_device_queue {
    chart_t           *chart;
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
    bool monitor_started;

    thread_t monitor_task;

    struct virtio_mmio_registers *mmio;
    virtio_feature_select_cb      feature_select_cb;

    uint32_t irq;
    uint32_t expected_device_id;

    uint32_t                    max_queues;
    uint32_t                    num_queues;
    struct virtio_device_queue *queues;
};

struct virtio_console {
    struct virtio_device *devptr;

    chart_t *control_rx;
    chart_t *control_tx;

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
void virtio_monitor_loop(struct virtio_device *device);

macro_define(VIRTIO_DEVICE_REGISTER,
             v_ident, v_region_id, v_device_id, v_feature_select, v_max_queues) {
    extern struct virtio_device v_ident;
    TASK_REGISTER(symbol_join(v_ident, task), virtio_monitor_loop, &v_ident, RESTARTABLE);
    struct virtio_device_queue symbol_join(v_ident, queues)[v_max_queues];
    struct virtio_device v_ident = {
        .initialized = false,
        .monitor_started = false,
        .monitor_task = &symbol_join(v_ident, task),
        .mmio = (struct virtio_mmio_registers *)
                    (VIRTIO_MMIO_ADDRESS_BASE + VIRTIO_MMIO_ADDRESS_STRIDE * (v_region_id)),
        .feature_select_cb = (v_feature_select),
        .irq = VIRTIO_MMIO_IRQS_BASE + (v_region_id),
        .expected_device_id = (v_device_id),
        .max_queues = (v_max_queues),
        .num_queues = 0, /* to be populated */
        .queues = symbol_join(v_ident, queues), /* to be populated */
    };
    PROGRAM_INIT_PARAM(STAGE_RAW, virtio_device_init_internal, v_ident, &v_ident);
    PROGRAM_INIT_PARAM(STAGE_READY, virtio_device_start_internal, v_ident, &v_ident)
}

macro_define(VIRTIO_DEVICE_SCHEDULE, v_ident, v_nanos) {
    TASK_SCHEDULE(symbol_join(v_ident, task), v_nanos)
}

// this may only be called before the scheduler starts
void virtio_device_setup_queue_internal(struct virtio_device *device, uint32_t queue_index);

macro_define(VIRTIO_DEVICE_QUEUE_REGISTER,
             v_ident, v_queue_index, v_direction, v_chart, v_queue_num) {
    static struct virtq_desc symbol_join(v_ident, v_queue_index, desc) [v_queue_num] __attribute__((__aligned__(16)));
    static struct {
        /* weird init syntax required due to flexible array member */
        struct virtq_avail avail;
        uint16_t flex_ring[v_queue_num];
    } symbol_join(v_ident, v_queue_index, avail) __attribute__((__aligned__(2)));
    static struct {
        /* weird init syntax required due to flexible array member */
        struct virtq_used used;
        struct virtq_used_elem ring[v_queue_num];
    } symbol_join(v_ident, v_queue_index, used) __attribute__((__aligned__(4)));
    static void symbol_join(v_ident, v_queue_index, init)(void) {
        assert(v_ident.queues[v_queue_index].chart == NULL);
        assert(v_ident.initialized == true && v_ident.monitor_started == false);
        assert((v_queue_index) < v_ident.num_queues);
        assert(chart_note_count(&v_chart) == (v_queue_num));
        v_ident.queues[v_queue_index].direction = (v_direction);
        v_ident.queues[v_queue_index].queue_num = (v_queue_num);
        v_ident.queues[v_queue_index].chart = &(v_chart);
        v_ident.queues[v_queue_index].desc  = symbol_join(v_ident, v_queue_index, desc);
        v_ident.queues[v_queue_index].avail = &symbol_join(v_ident, v_queue_index, avail).avail;
        v_ident.queues[v_queue_index].used  = &symbol_join(v_ident, v_queue_index, used).used;
        virtio_device_setup_queue_internal(&v_ident, v_queue_index);
    }
    PROGRAM_INIT(STAGE_READY, symbol_join(v_ident, v_queue_index, init));
}

void virtio_console_feature_select(uint64_t *features);
void virtio_console_control_loop(struct virtio_console *console);
void virtio_console_configure_internal(struct virtio_console *console);

macro_define(VIRTIO_CONSOLE_REGISTER,
             v_ident, v_region_id, v_data_rx, v_data_tx, v_rx_num, v_tx_num) {
    VIRTIO_DEVICE_REGISTER(symbol_join(v_ident, device), v_region_id, VIRTIO_CONSOLE_ID, virtio_console_feature_select,
                           6 /* room for queues 2,3,4,5 needed later */);
    extern struct virtio_console v_ident;
    TASK_REGISTER(symbol_join(v_ident, task), virtio_console_control_loop, &v_ident, NOT_RESTARTABLE);
    CHART_REGISTER(symbol_join(v_ident, crx), sizeof(struct virtio_console_control) + sizeof(struct io_rx_ent)
                                        + VIRTIO_CONSOLE_CTRL_RECV_MARGIN, 4);
    CHART_REGISTER(symbol_join(v_ident, ctx), sizeof(struct virtio_console_control) + sizeof(struct io_tx_ent), 4);
    CHART_SERVER_NOTIFY(symbol_join(v_ident, crx), task_rouse, &symbol_join(v_ident, task));
    CHART_CLIENT_NOTIFY(symbol_join(v_ident, ctx), task_rouse, &symbol_join(v_ident, task));
    VIRTIO_DEVICE_QUEUE_REGISTER(symbol_join(v_ident, device), 2, /* control + rx */
                                 QUEUE_INPUT,  symbol_join(v_ident, crx), 4);
    VIRTIO_DEVICE_QUEUE_REGISTER(symbol_join(v_ident, device), 3, /* control + tx */
                                 QUEUE_OUTPUT, symbol_join(v_ident, ctx), 4);
    VIRTIO_DEVICE_QUEUE_REGISTER(symbol_join(v_ident, device), 4, /* data[1] + rx */
                                 QUEUE_INPUT,  v_data_rx, v_rx_num);
    VIRTIO_DEVICE_QUEUE_REGISTER(symbol_join(v_ident, device), 5, /* data[1] + tx */
                                 QUEUE_OUTPUT, v_data_tx, v_tx_num);
    struct virtio_console v_ident = {
        .devptr = &symbol_join(v_ident, device),
        .control_rx = &symbol_join(v_ident, crx),
        .control_tx = &symbol_join(v_ident, ctx),
        .confirmed_port_present = false,
    };
    PROGRAM_INIT_PARAM(STAGE_CRAFT, virtio_console_configure_internal, v_ident, &v_ident)
}

// We have to schedule serial-ctrl before the virtio monitor, because while it isn't needed
// during regular execution, it is on the critical path for activating the spacecraft bus.
// The very first message it sends MUST go out immediately!
macro_define(VIRTIO_CONSOLE_SCHEDULE, v_ident) {
    TASK_SCHEDULE(symbol_join(v_ident, task), 25)
    VIRTIO_DEVICE_SCHEDULE(symbol_join(v_ident, device), 75)
}

void *virtio_device_config_space(struct virtio_device *device);

// for a queue already set up using virtio_device_setup_queue, this function spuriously notifies the queue.
void virtio_device_force_notify_queue(struct virtio_device *device, uint32_t queue_index);

#endif /* FSW_FREERTOS_RTOS_VIRTIO_H */
