#ifndef FSW_FREERTOS_RTOS_VIRTIO_H
#define FSW_FREERTOS_RTOS_VIRTIO_H

#include <stdint.h>
#include <stdbool.h>

#include <FreeRTOS.h>
#include <task.h>

#include <rtos/gic.h>
#include <hal/thread.h>
#include <fsw/chart.h>
#include <fsw/init.h>

enum {
    // configuration for the particular VIRTIO MMIO layout of the qemu-system-arm -M virt simulation board
    VIRTIO_MMIO_ADDRESS_BASE   = 0x0A000000,
    VIRTIO_MMIO_ADDRESS_STRIDE = 0x200,
    VIRTIO_MMIO_IRQS_BASE      = IRQ_SPI_BASE + 16,
    VIRTIO_MMIO_REGION_NUM     = 32,

    // VIRTIO type for a console device
    VIRTIO_CONSOLE_ID = 3,
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

    uint32_t                    num_queues;
    struct virtio_device_queue *queues;
};

struct virtio_console {
    bool initialized;

    struct virtio_device *devptr;

    thread_t control_task;
    chart_t  control_rx;
    chart_t  control_tx;

    bool confirmed_port_present;
};

void virtio_device_init_internal(struct virtio_device *device);
void virtio_device_start_internal(struct virtio_device *device);
void virtio_monitor_loop(struct virtio_device *device);

#define VIRTIO_DEVICE_REGISTER(v_ident, v_region_id, v_device_id, v_feature_select) \
    extern struct virtio_device v_ident;                       \
    TASK_REGISTER(v_ident ## _task, "virtio-monitor", PRIORITY_DRIVERS, virtio_monitor_loop, &v_ident, RESTARTABLE); \
    struct virtio_device v_ident = {                           \
        .initialized = false,                                  \
        .monitor_started = false,                              \
        .monitor_task = &v_ident ## _task,                     \
        .mmio = (struct virtio_mmio_registers *)               \
                    (VIRTIO_MMIO_ADDRESS_BASE + VIRTIO_MMIO_ADDRESS_STRIDE * (v_region_id)),                         \
        .feature_select_cb = (v_feature_select),               \
        .irq = VIRTIO_MMIO_IRQS_BASE + (v_region_id),          \
        .expected_device_id = (v_device_id),                   \
        .num_queues = 0, /* to be populated */                 \
        .queues = NULL, /* to be populated */                  \
    };                                                         \
    PROGRAM_INIT_PARAM(STAGE_RAW, virtio_device_init_internal, v_ident, &v_ident);                                   \
    PROGRAM_INIT_PARAM(STAGE_READY, virtio_device_start_internal, v_ident, &v_ident)

void virtio_console_feature_select(uint64_t *features);
void virtio_console_control_loop(struct virtio_console *console);

#define VIRTIO_CONSOLE_REGISTER(v_ident, v_region_id) \
    VIRTIO_DEVICE_REGISTER(v_ident ## _device, v_region_id, VIRTIO_CONSOLE_ID, virtio_console_feature_select); \
    extern struct virtio_console v_ident;                                    \
    TASK_REGISTER(v_ident ## _task, "serial-ctrl", PRIORITY_INIT,            \
                  virtio_console_control_loop, &v_ident, NOT_RESTARTABLE);   \
    struct virtio_console v_ident = {                                        \
        .initialized = false,                                                \
        .devptr = &v_ident ## _device,                                       \
        .control_task = &v_ident ## _task,                                   \
        /* control_rx, and control_tx are initialized later */               \
        .confirmed_port_present = false,                                     \
    }

void *virtio_device_config_space(struct virtio_device *device);

// this may only be called before the scheduler starts
void virtio_device_setup_queue(struct virtio_device *device, uint32_t queue_index, virtio_queue_dir_t direction, chart_t *chart);

// for a queue already set up using virtio_device_setup_queue, this function spuriously notifies the queue.
void virtio_device_force_notify_queue(struct virtio_device *device, uint32_t queue_index);

// must be called on a console previously registered using VIRTIO_CONSOLE_REGISTER
void virtio_console_init(struct virtio_console *console, chart_t *data_rx, chart_t *data_tx);

#endif /* FSW_FREERTOS_RTOS_VIRTIO_H */
