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

#define VIRTIO_INPUT_QUEUE_REPLICAS 1

enum {
    // configuration for the particular VIRTIO MMIO layout of the qemu-system-arm -M virt simulation board
    VIRTIO_MMIO_ADDRESS_BASE   = 0x0A000000,
    VIRTIO_MMIO_ADDRESS_STRIDE = 0x200,
    VIRTIO_MMIO_IRQS_BASE      = IRQ_SPI_BASE + 16,
    VIRTIO_MMIO_REGION_NUM     = 32,
};

// read the features, write back selected features, or abort/assert if features are not acceptable
typedef void (*virtio_feature_select_cb)(uint64_t *features);

typedef const struct {
    struct virtio_mmio_registers *mmio;
    virtio_feature_select_cb      feature_select_cb;

    uint32_t irq;
    uint32_t expected_device_id;
} virtio_device_t;

// If a queue is an INPUT queue (i.e. it reads from the device),
//     then virtio is the duct SENDER and the other end is the duct RECEIVER.
typedef const struct {
    struct virtio_device_input_queue_prepare_mut {
        uint16_t new_used_idx;
    } *prepare_mut;

    struct virtq_desc  *desc;
    struct virtq_avail *avail;
    struct virtq_used  *used;

    virtio_device_t *parent_device;
    uint32_t         queue_index;
    size_t           message_size;

    uint8_t *receive_buffer;
    duct_t  *mut_duct;
    size_t   queue_num;
} virtio_device_input_queue_singletons_t;

typedef const struct {
    struct virtio_device_input_queue_prepare_mut *prepare_mut;

    uint8_t  replica_id;
    duct_t  *mut_duct; // for passing forward last_used_idx as state to other replicas
    duct_t  *io_duct;
    size_t   message_size; // duct_message_size(io_duct)
    size_t   queue_num;
    uint8_t *receive_buffer;
    uint8_t *merge_buffer; // of size message_size

    struct virtq_used *used;
} virtio_device_input_queue_replica_t;

// If a queue is an OUTPUT queue (i.e. it writes to the device),
//     then virtio is the duct RECEIVER and the other end is the duct SENDER.
typedef const struct {
    struct virtio_device_output_queue_mut {
        uint16_t last_used_idx;
    } *mut;
    virtio_device_t *parent_device;
    uint32_t         queue_index;

    duct_t  *duct;
    uint8_t *buffer; // size is the same as the queue max flow * duct message size
    size_t   queue_num;

    struct virtq_desc  *desc;
    struct virtq_avail *avail;
    struct virtq_used  *used;
} virtio_device_output_queue_t;

void virtio_device_init_internal(virtio_device_t *device);
void virtio_monitor_clip(virtio_device_t *device);

macro_define(VIRTIO_DEVICE_REGISTER,
             v_ident, v_region_id, v_device_id, v_feature_select) {
    virtio_device_t v_ident = {
        .mmio = (struct virtio_mmio_registers *)
                    (VIRTIO_MMIO_ADDRESS_BASE + VIRTIO_MMIO_ADDRESS_STRIDE * (v_region_id)),
        .feature_select_cb = (v_feature_select),
        .irq = VIRTIO_MMIO_IRQS_BASE + (v_region_id),
        .expected_device_id = (v_device_id),
    };
    PROGRAM_INIT_PARAM(STAGE_RAW, virtio_device_init_internal, v_ident, &v_ident)
}

void virtio_device_setup_queue_internal(struct virtio_mmio_registers *mmio, uint32_t queue_index, size_t queue_num,
                                        struct virtq_desc *desc, struct virtq_avail *avail, struct virtq_used *used);
void virtio_input_queue_prepare_clip(virtio_device_input_queue_singletons_t *queue);
void virtio_input_queue_advance_clip(virtio_device_input_queue_replica_t *queue);
void virtio_input_queue_commit_clip(virtio_device_input_queue_singletons_t *queue);
void virtio_output_queue_monitor_clip(virtio_device_output_queue_t *queue);

macro_define(VIRTIO_DEVICE_QUEUE_COMMON,
             v_ident, v_queue_index, v_duct, v_duct_flow, v_queue_flow, v_duct_capacity, v_initial_avail_idx) {
    static struct virtq_desc symbol_join(v_ident, v_queue_index, desc)[v_queue_flow] __attribute__((__aligned__(16)));
    static struct {
        /* weird init syntax required due to flexible array member */
        struct virtq_avail avail;
        uint16_t flex_ring[v_queue_flow];
    } symbol_join(v_ident, v_queue_index, avail) __attribute__((__aligned__(2))) = {
        // TODO: can this be eliminated? the regular input clip should now be able to populate these all correctly.
        .avail = {
            .flags = 0,
            .idx   = (v_initial_avail_idx),
        },
        // populate all of the avail ring entries to point to their corresponding descriptors.
        // we'll ensure these stay constant.
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
    static void symbol_join(v_ident, v_queue_index, init)(void) {
        assert(duct_max_flow(&v_duct) == (v_duct_flow));
        assert(duct_message_size(&v_duct) == (v_duct_capacity));
        virtio_device_setup_queue_internal(v_ident.mmio, v_queue_index, v_queue_flow,
                                           symbol_join(v_ident, v_queue_index, desc),
                                           &symbol_join(v_ident, v_queue_index, avail).avail,
                                           &symbol_join(v_ident, v_queue_index, used).used);
    }
    PROGRAM_INIT(STAGE_READY, symbol_join(v_ident, v_queue_index, init))
}

macro_define(VIRTIO_DEVICE_INPUT_QUEUE_REGISTER,
             v_ident, v_queue_index, v_duct, v_duct_flow, v_queue_flow, v_duct_capacity) {
    static_assert((v_duct_flow) <= (v_queue_flow), "merging can only reduce number of duct entries needed");
    VIRTIO_DEVICE_QUEUE_COMMON(v_ident, v_queue_index, v_duct,
                               v_duct_flow, v_queue_flow, v_duct_capacity, 0);
    struct virtio_device_input_queue_prepare_mut symbol_join(v_ident, v_queue_index, prepare_mut) = {
        .new_used_idx = 0,
    };
    DUCT_REGISTER(symbol_join(v_ident, v_queue_index, mut_duct),
                  VIRTIO_INPUT_QUEUE_REPLICAS, VIRTIO_INPUT_QUEUE_REPLICAS + 1,
                  1, sizeof(uint16_t), DUCT_RECEIVER_FIRST); // TODO: DUCT_RECEIVER_FIRST isn't quite right for the split
    uint8_t symbol_join(v_ident, v_queue_index, receive_buffer)[(v_queue_flow) * (v_duct_capacity)];
    virtio_device_input_queue_singletons_t symbol_join(v_ident, v_queue_index, singleton_data) = {
        .prepare_mut = &symbol_join(v_ident, v_queue_index, prepare_mut),

        .desc = symbol_join(v_ident, v_queue_index, desc),
        .avail = &symbol_join(v_ident, v_queue_index, avail).avail,
        .used = &symbol_join(v_ident, v_queue_index, used).used,

        .parent_device = &v_ident,
        .queue_index = (v_queue_index),
        .message_size = (v_duct_capacity),

        .receive_buffer = symbol_join(v_ident, v_queue_index, receive_buffer),
        .mut_duct = &symbol_join(v_ident, v_queue_index, mut_duct),
        .queue_num = (v_queue_flow),
    };

    CLIP_REGISTER(symbol_join(v_ident, v_queue_index, prepare_clip),
                  virtio_input_queue_prepare_clip, &symbol_join(v_ident, v_queue_index, singleton_data));
    static_repeat(VIRTIO_INPUT_QUEUE_REPLICAS, v_replica_id) {
        uint8_t symbol_join(v_ident, v_queue_index, replica, v_replica_id, merge_buffer)[v_duct_capacity];
        virtio_device_input_queue_replica_t symbol_join(v_ident, v_queue_index, replica, v_replica_id) = {
            .prepare_mut = &symbol_join(v_ident, v_queue_index, prepare_mut),

            .replica_id = v_replica_id,
            .mut_duct = &symbol_join(v_ident, v_queue_index, mut_duct),
            .io_duct = &(v_duct),
            .message_size = (v_duct_capacity),
            .queue_num = (v_queue_flow),
            .receive_buffer = symbol_join(v_ident, v_queue_index, receive_buffer),
            .merge_buffer = symbol_join(v_ident, v_queue_index, replica, v_replica_id, merge_buffer),
            .used = &symbol_join(v_ident, v_queue_index, used).used,
        };
        CLIP_REGISTER(symbol_join(v_ident, v_queue_index, advance_clip, v_replica_id),
                      virtio_input_queue_advance_clip, &symbol_join(v_ident, v_queue_index, replica, v_replica_id));
    }
    CLIP_REGISTER(symbol_join(v_ident, v_queue_index, commit_clip),
                  virtio_input_queue_commit_clip, &symbol_join(v_ident, v_queue_index, singleton_data))
}

macro_define(VIRTIO_DEVICE_OUTPUT_QUEUE_REGISTER,
             v_ident, v_queue_index, v_duct, v_duct_flow, v_duct_capacity) {
    VIRTIO_DEVICE_QUEUE_COMMON(v_ident, v_queue_index, v_duct,
                               v_duct_flow, v_duct_flow, v_duct_capacity, 0);
    struct virtio_device_output_queue_mut symbol_join(v_ident, v_queue_index, queue_mutable) = {
        .last_used_idx = 0,
    };
    static uint8_t symbol_join(v_ident, v_queue_index, transmit_buffer)[(v_duct_flow) * (v_duct_capacity)];
    virtio_device_output_queue_t symbol_join(v_ident, v_queue_index, queue) = {
        .mut = &symbol_join(v_ident, v_queue_index, queue_mutable),
        .parent_device = &v_ident,
        .queue_index = (v_queue_index),
        .duct = &(v_duct),
        .buffer = symbol_join(v_ident, v_queue_index, transmit_buffer),
        .queue_num = (v_duct_flow),
        .desc = symbol_join(v_ident, v_queue_index, desc),
        .avail = &symbol_join(v_ident, v_queue_index, avail).avail,
        .used = &symbol_join(v_ident, v_queue_index, used).used,
    };
    CLIP_REGISTER(symbol_join(v_ident, v_queue_index, monitor_clip),
                  virtio_output_queue_monitor_clip, &symbol_join(v_ident, v_queue_index, queue))
}

macro_define(VIRTIO_DEVICE_INPUT_QUEUE_REF, v_ident, v_queue_index) {
    (&symbol_join(v_ident, v_queue_index, singleton_data))
}

macro_define(VIRTIO_DEVICE_INPUT_QUEUE_SCHEDULE, v_ident, v_queue_index) {
    CLIP_SCHEDULE(symbol_join(v_ident, v_queue_index, prepare_clip), 5)
    static_repeat(VIRTIO_INPUT_QUEUE_REPLICAS, v_replica_id) {
        CLIP_SCHEDULE(symbol_join(v_ident, v_queue_index, advance_clip, v_replica_id), 20)
    }
    CLIP_SCHEDULE(symbol_join(v_ident, v_queue_index, commit_clip), 5)
}

macro_define(VIRTIO_DEVICE_OUTPUT_QUEUE_SCHEDULE, v_ident, v_queue_index, v_nanos) {
    CLIP_SCHEDULE(symbol_join(v_ident, v_queue_index, monitor_clip), v_nanos)
}

void *virtio_device_config_space(virtio_device_t *device);

// for a queue already set up using virtio_device_setup_queue, this function spuriously notifies the queue.
void virtio_device_force_notify_queue(virtio_device_input_queue_singletons_t *queue);

#endif /* FSW_VIVID_RTOS_VIRTIO_H */
