#ifndef FSW_VIVID_RTOS_VIRTIO_H
#define FSW_VIVID_RTOS_VIRTIO_H

#include <endian.h>
#include <stdint.h>
#include <stdbool.h>

#include <FreeRTOS.h>
#include <task.h>

#include <rtos/gic.h>
#include <rtos/virtqueue.h>
#include <hal/thread.h>
#include <hal/init.h>
#include <synch/duct.h>
#include <synch/notepad.h>

#define VIRTIO_INPUT_QUEUE_REPLICAS 3

// NOTE: this is NOT a configuration option; this is the number used because there is one prepare clip and one commit
// clip. The code would have to be restructured for this to be anything else.
#define VIRTIO_OUTPUT_QUEUE_REPLICAS 2

enum {
    // configuration for the particular VIRTIO MMIO layout of the qemu-system-arm -M virt simulation board
    VIRTIO_MMIO_ADDRESS_BASE   = 0x0A000000,
    VIRTIO_MMIO_ADDRESS_STRIDE = 0x200,
    VIRTIO_MMIO_IRQS_BASE      = IRQ_SPI_BASE + 16,
    VIRTIO_MMIO_REGION_NUM     = 32,
};

// read the features, write back selected features, or abort/assert if features are not acceptable
typedef void (*virtio_feature_select_cb)(uint64_t *features);

// all of these are little-endian
struct virtio_mmio_registers {
    const volatile uint32_t magic_value;         // Magic value (R)
    const volatile uint32_t version;             // Device version number (R)
    const volatile uint32_t device_id;           // Virtio Subsystem Device ID (R)
    const volatile uint32_t vendor_id;           // Virtio Subsystem Vendor ID (R)
    const volatile uint32_t device_features;     // Flags representing features the device supports (R)
          volatile uint32_t device_features_sel; // Device (host) features word selection (W)
                   uint32_t RESERVED_0[2];
          volatile uint32_t driver_features;     // Flags representing device features understood and activated by the driver (W)
          volatile uint32_t driver_features_sel; // Activated (guest) features word selection (W)
                   uint32_t RESERVED_1[2];
          volatile uint32_t queue_sel;           // Virtual queue index (W)
    const volatile uint32_t queue_num_max;       // Maximum virtual queue size (R)
          volatile uint32_t queue_num;           // Virtual queue size (W)
                   uint32_t RESERVED_2[2];
          volatile uint32_t queue_ready;         // Virtual queue ready bit (RW)
                   uint32_t RESERVED_3[2];
          volatile uint32_t queue_notify;        // Queue notifier (W)
                   uint32_t RESERVED_4[3];
    const volatile uint32_t interrupt_status;    // Interrupt status (R)
          volatile uint32_t interrupt_ack;       // Interrupt acknowledge (W)
                   uint32_t RESERVED_5[2];
          volatile uint32_t status;              // Device status (RW)
                   uint32_t RESERVED_6[3];
          volatile uint64_t queue_desc;          // Virtual queue's Descriptor Area 64 bit long physical address (W)
                   uint32_t RESERVED_7[2];
          volatile uint64_t queue_driver;        // Virtual queue's Driver Area 64 bit long physical address (W)
                   uint32_t RESERVED_8[2];
          volatile uint64_t queue_device;        // Virtual queue's Device Area 64 bit long physical address (W)
                   uint32_t RESERVED_9[21];
    const volatile uint32_t config_generation;   // Configuration atomicity value (R)
};
static_assert(sizeof(struct virtio_mmio_registers) == 0x100, "wrong sizeof(struct virtio_mmio_registers)");

typedef const struct {
    struct virtio_mmio_registers *mmio;
    virtio_feature_select_cb      feature_select_cb;

    uint32_t irq;
    uint32_t expected_device_id;
} virtio_device_t;

struct virtio_device_input_queue_notepad_mutable {
    uint16_t last_used_idx;
};

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
    size_t           queue_num;

    uint8_t       *receive_buffer;
    notepad_ref_t *mut_observer;
} virtio_device_input_queue_singletons_t;

typedef const struct {
    struct virtio_device_input_queue_prepare_mut *prepare_mut;

    uint8_t  replica_id;
    duct_t  *io_duct;
    size_t   message_size; // duct_message_size(io_duct)
    size_t   queue_num;
    uint8_t *receive_buffer;
    uint8_t *merge_buffer; // of size message_size

    struct virtq_used *used;
    notepad_ref_t     *mut_replica;
} virtio_device_input_queue_replica_t;

// If a queue is an OUTPUT queue (i.e. it writes to the device),
//     then virtio is the duct RECEIVER and the other end is the duct SENDER.
typedef const struct {
    virtio_device_t *parent_device;
    uint32_t         queue_index;

    duct_t  *duct;
    uint8_t *transmit_buffer; // size is the same as the queue max flow * duct message size
    uint8_t *compare_buffer;  // size is the same as the duct message size
    size_t   message_size;    // same as the duct message size
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
void virtio_output_queue_prepare_clip(virtio_device_output_queue_t *queue);
void virtio_output_queue_commit_clip(virtio_device_output_queue_t *queue);

macro_define(VIRTIO_DEVICE_QUEUE_COMMON,
             v_ident, v_queue_index, v_duct, v_duct_flow, v_queue_flow, v_duct_capacity, v_initial_avail_idx) {
    static_assert(((v_queue_flow) & ((v_queue_flow) - 1)) == 0, "per virtio spec, queue flow must be a power of 2");
    struct virtq_desc symbol_join(v_ident, v_queue_index, desc)[v_queue_flow] __attribute__((__aligned__(16)));
    struct {
        /* weird init syntax required due to flexible array member */
        struct virtq_avail avail;
        uint16_t flex_ring[v_queue_flow];
    } symbol_join(v_ident, v_queue_index, avail) __attribute__((__aligned__(2))) = {
        // TODO: can this be eliminated? the regular input clip should now be able to populate these all correctly.
        .avail = {
            .flags = htole16(0),
            .idx   = htole16(v_initial_avail_idx),
        },
        // populate all of the avail ring entries to point to their corresponding descriptors.
        // we'll ensure these stay constant.
        .flex_ring = {
            static_repeat(v_queue_flow, desc_idx) {
                htole16(desc_idx),
            }
        },
    };
    struct {
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
    NOTEPAD_REGISTER(symbol_join(v_ident, v_queue_index, mut_notepad),
                     VIRTIO_INPUT_QUEUE_REPLICAS, 1, sizeof(struct virtio_device_input_queue_notepad_mutable));
    uint8_t symbol_join(v_ident, v_queue_index, receive_buffer)[(v_queue_flow) * (v_duct_capacity)];
    virtio_device_input_queue_singletons_t symbol_join(v_ident, v_queue_index, singleton_data) = {
        .prepare_mut = &symbol_join(v_ident, v_queue_index, prepare_mut),

        .desc = symbol_join(v_ident, v_queue_index, desc),
        .avail = &symbol_join(v_ident, v_queue_index, avail).avail,
        .used = &symbol_join(v_ident, v_queue_index, used).used,

        .parent_device = &v_ident,
        .queue_index = (v_queue_index),
        .message_size = (v_duct_capacity),
        .queue_num = (v_queue_flow),

        .receive_buffer = symbol_join(v_ident, v_queue_index, receive_buffer),
        .mut_observer = NOTEPAD_OBSERVER_REF(symbol_join(v_ident, v_queue_index, mut_notepad), 0),
    };

    CLIP_REGISTER(symbol_join(v_ident, v_queue_index, prepare_clip),
                  virtio_input_queue_prepare_clip, &symbol_join(v_ident, v_queue_index, singleton_data));
    static_repeat(VIRTIO_INPUT_QUEUE_REPLICAS, v_replica_id) {
        uint8_t symbol_join(v_ident, v_queue_index, replica, v_replica_id, merge_buffer)[v_duct_capacity];
        virtio_device_input_queue_replica_t symbol_join(v_ident, v_queue_index, replica, v_replica_id) = {
            .prepare_mut = &symbol_join(v_ident, v_queue_index, prepare_mut),

            .replica_id = v_replica_id,
            .io_duct = &(v_duct),
            .message_size = (v_duct_capacity),
            .queue_num = (v_queue_flow),
            .receive_buffer = symbol_join(v_ident, v_queue_index, receive_buffer),
            .merge_buffer = symbol_join(v_ident, v_queue_index, replica, v_replica_id, merge_buffer),

            .mut_replica = NOTEPAD_REPLICA_REF(symbol_join(v_ident, v_queue_index, mut_notepad), v_replica_id),
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
    uint8_t symbol_join(v_ident, v_queue_index, transmit_buffer)[(v_duct_flow) * (v_duct_capacity)];
    uint8_t symbol_join(v_ident, v_queue_index, compare_buffer)[v_duct_capacity];
    virtio_device_output_queue_t symbol_join(v_ident, v_queue_index, queue) = {
        .parent_device = &v_ident,
        .queue_index = (v_queue_index),

        .duct = &(v_duct),
        .transmit_buffer = symbol_join(v_ident, v_queue_index, transmit_buffer),
        .compare_buffer = symbol_join(v_ident, v_queue_index, compare_buffer),
        .message_size = (v_duct_capacity),
        .queue_num = (v_duct_flow),

        .desc = symbol_join(v_ident, v_queue_index, desc),
        .avail = &symbol_join(v_ident, v_queue_index, avail).avail,
        .used = &symbol_join(v_ident, v_queue_index, used).used,
    };
    CLIP_REGISTER(symbol_join(v_ident, v_queue_index, prepare_clip),
                  virtio_output_queue_prepare_clip, &symbol_join(v_ident, v_queue_index, queue));
    CLIP_REGISTER(symbol_join(v_ident, v_queue_index, commit_clip),
                  virtio_output_queue_commit_clip, &symbol_join(v_ident, v_queue_index, queue))
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
    CLIP_SCHEDULE(symbol_join(v_ident, v_queue_index, prepare_clip), v_nanos)
    CLIP_SCHEDULE(symbol_join(v_ident, v_queue_index, commit_clip), v_nanos)
}

void *virtio_device_config_space(virtio_device_t *device);

// for a queue already set up using virtio_device_setup_queue, this function spuriously notifies the queue.
void virtio_device_force_notify_queue(virtio_device_input_queue_singletons_t *queue);

#endif /* FSW_VIVID_RTOS_VIRTIO_H */
