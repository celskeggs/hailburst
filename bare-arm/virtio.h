#ifndef BARE_ARM_VIRTIO_H
#define BARE_ARM_VIRTIO_H

#include <stdint.h>
#include <stdbool.h>

enum {
    VIRTIO_MMIO_ADDRESS   = 0x0A003E00,
    VIRTIO_MMIO_IRQ       = 47,
};

struct virtio_console {
    TaskHandle_t monitor_task;

    struct virtio_mmio_registers *mmio;
    struct virtio_console_config *config;
    uint32_t irq;

    size_t num_queues;
    struct virtq *virtqueues;
};

bool virtio_init(struct virtio_console *con, uintptr_t mem_addr, uint32_t irq);

#endif /* BARE_ARM_VIRTIO_H */
