#ifndef FSW_FREERTOS_RTOS_VIRTIO_H
#define FSW_FREERTOS_RTOS_VIRTIO_H

#include <stdint.h>
#include <stdbool.h>

#include <FreeRTOS.h>
#include <task.h>

#include <rtos/gic.h>

enum {
    VIRTIO_MMIO_ADDRESS_BASE   = 0x0A000000,
    VIRTIO_MMIO_ADDRESS_STRIDE = 0x200,
    VIRTIO_MMIO_IRQS_BASE      = IRQ_SPI_BASE + 16,
    VIRTIO_MMIO_REGION_NUM     = 32,
};

struct virtio_console_port;

// called when a new port becomes available
typedef void (*virtio_port_cb)(void *param, struct virtio_console_port *con);

struct virtio_console {
    TaskHandle_t monitor_task;

    struct virtio_mmio_registers *mmio;
    struct virtio_console_config *config;
    uint32_t irq;

    virtio_port_cb callback;
    void          *callback_param;

    size_t num_queues;
    struct virtq *virtqueues;
};

struct virtio_console_port {
    struct virtio_console *console;
    size_t port_num;
    struct virtq *receiveq;
    struct virtq *transmitq;
};

struct vector_entry {
    void *data_buffer;
    size_t length;
    bool is_receive;
};

void virtio_init(virtio_port_cb callback, void *param);
void virtio_serial_ready(struct virtio_console_port *port);
// if negative, no room in ring buffer for descriptor; otherwise, returns number of bytes written by device.
ssize_t virtio_transact_sync(struct virtq *vq, struct vector_entry *ents, size_t ent_count, uint64_t *timestamp_ns_out);

#endif /* FSW_FREERTOS_RTOS_VIRTIO_H */
