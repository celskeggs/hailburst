#ifndef FSW_VIVID_RTOS_VIRTIO_H
#define FSW_VIVID_RTOS_VIRTIO_H

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
};

typedef enum {
    QUEUE_INPUT  = 1,
    QUEUE_OUTPUT = 2,
} virtio_queue_dir_t;

// read the features, write back selected features, or abort/assert if features are not acceptable
typedef void (*virtio_feature_select_cb)(uint64_t *features);

typedef const struct {
    struct virtio_device_mut {
        bool initialized;
        uint32_t num_queues;
    } *mut;

    struct virtio_mmio_registers *mmio;
    virtio_feature_select_cb      feature_select_cb;

    uint32_t irq;
    uint32_t expected_device_id;
} virtio_device_t;

// If a queue is an INPUT queue (i.e. it reads from the device),
//     then virtio is the duct SENDER and the other end is the duct RECEIVER.
// If a queue is an OUTPUT queue (i.e. it writes to the device),
//     then virtio is the duct RECEIVER and the other end is the duct SENDER.
typedef const struct {
    struct virtio_device_queue_mut {
        uint16_t last_used_idx;
    } *mut;
    virtio_device_t *parent_device;
    uint32_t         queue_index;

    duct_t            *duct;
    uint8_t           *buffer; // size is the same as the queue max flow * duct message size
    virtio_queue_dir_t direction;

    size_t   queue_num;

    struct virtq_desc  *desc;
    struct virtq_avail *avail;
    struct virtq_used  *used;
} virtio_device_queue_t;

void virtio_device_init_internal(virtio_device_t *device);
void virtio_monitor_clip(virtio_device_t *device);

macro_define(VIRTIO_DEVICE_REGISTER,
             v_ident, v_region_id, v_device_id, v_feature_select) {
    struct virtio_device_mut symbol_join(v_ident, mut) = {
        .initialized = false,
        .num_queues = 0, /* to be populated */
    };
    virtio_device_t v_ident = {
        .mut = &symbol_join(v_ident, mut),
        .mmio = (struct virtio_mmio_registers *)
                    (VIRTIO_MMIO_ADDRESS_BASE + VIRTIO_MMIO_ADDRESS_STRIDE * (v_region_id)),
        .feature_select_cb = (v_feature_select),
        .irq = VIRTIO_MMIO_IRQS_BASE + (v_region_id),
        .expected_device_id = (v_device_id),
    };
    PROGRAM_INIT_PARAM(STAGE_RAW, virtio_device_init_internal, v_ident, &v_ident)
}

void virtio_device_setup_queue_internal(virtio_device_queue_t *queue);
void virtio_queue_monitor_clip(virtio_device_queue_t *queue);

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
    struct virtio_device_queue_mut symbol_join(v_ident, v_queue_index, queue_mutable) = {
        .last_used_idx = 0,
    };
    virtio_device_queue_t symbol_join(v_ident, v_queue_index, queue) = {
        .mut = &symbol_join(v_ident, v_queue_index, queue_mutable),
        .parent_device = &v_ident,
        .queue_index = (v_queue_index),
        .duct = &(v_duct),
        .buffer = symbol_join(v_ident, v_queue_index, buffer),
        .direction = (v_direction),
        .queue_num = (v_queue_flow),
        .desc = symbol_join(v_ident, v_queue_index, desc),
        .avail = &symbol_join(v_ident, v_queue_index, avail).avail,
        .used = &symbol_join(v_ident, v_queue_index, used).used,
    };
    static void symbol_join(v_ident, v_queue_index, init)(void) {
        assert(v_ident.mut->initialized == true);
        assert((v_queue_index) < v_ident.mut->num_queues);
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
    CLIP_REGISTER(symbol_join(v_ident, v_queue_index, monitor_clip),
                  virtio_queue_monitor_clip, &symbol_join(v_ident, v_queue_index, queue))
}

macro_define(VIRTIO_DEVICE_QUEUE_REF, v_ident, v_queue_index) {
    (&symbol_join(v_ident, v_queue_index, queue))
}

macro_define(VIRTIO_DEVICE_QUEUE_SCHEDULE, v_ident, v_queue_index, v_nanos) {
    CLIP_SCHEDULE(symbol_join(v_ident, v_queue_index, monitor_clip), v_nanos)
}

void *virtio_device_config_space(virtio_device_t *device);

// for a queue already set up using virtio_device_setup_queue, this function spuriously notifies the queue.
void virtio_device_force_notify_queue(virtio_device_queue_t *queue);

#endif /* FSW_VIVID_RTOS_VIRTIO_H */
