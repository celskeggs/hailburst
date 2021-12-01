#ifndef FSW_FREERTOS_RTOS_VIRTIO_H
#define FSW_FREERTOS_RTOS_VIRTIO_H

#include <stdint.h>
#include <stdbool.h>

#include <FreeRTOS.h>
#include <task.h>

#include <rtos/gic.h>
#include <hal/thread.h>
#include <fsw/chart.h>

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

struct virtio_device {
    bool initialized;

    thread_t monitor_task;

    struct virtio_mmio_registers *mmio;
    void                         *config_space;
    uint32_t                      irq;

    uint32_t                    num_queues;
    struct virtio_device_queue *queues;
};

struct virtio_console {
    bool initialized;

    struct virtio_device device;

    semaphore_t control_wake;
    thread_t    control_task;
    chart_t     control_rx;
    chart_t     control_tx;

    bool confirmed_port_present;
};

// read the features, write back selected features, and return true to indicate success.
typedef bool (*virtio_feature_select_cb)(uint64_t *features);

// true on success, false on failure
bool virtio_device_init(struct virtio_device *device, uintptr_t mem_addr, uint32_t irq, uint32_t device_id, virtio_feature_select_cb feature_select);
void *virtio_device_config_space(struct virtio_device *device);
// true on success, false on failure
bool virtio_device_setup_queue(struct virtio_device *device, uint32_t queue_index, virtio_queue_dir_t direction, chart_t *chart);
// for a queue already set up using virtio_device_setup_queue, this function spuriously notifies the queue.
void virtio_device_force_notify_queue(struct virtio_device *device, uint32_t queue_index);
void virtio_device_start(struct virtio_device *device);
void virtio_device_fail(struct virtio_device *device);

bool virtio_console_init(struct virtio_console *console, chart_t *data_rx, chart_t *data_tx);

#endif /* FSW_FREERTOS_RTOS_VIRTIO_H */
