#include <endian.h>
#include <stddef.h>
#include <stdint.h>

#include <rtos/gic.h>
#include <rtos/virtio.h>
#include <hal/atomic.h>
#include <hal/debug.h>
#include <hal/thread.h>
#include <hal/timer.h>

//#define DEBUG_VIRTQ

enum {
    VIRTIO_MAGIC_VALUE    = 0x74726976,
    VIRTIO_LEGACY_VERSION = 1,
    VIRTIO_VERSION        = 2,

    VIRTIO_IRQ_BIT_USED_BUFFER = 0x1,
    VIRTIO_IRQ_BIT_CONF_CHANGE = 0x2,

    REPLICA_ID = 0,
};

enum {
    VIRTIO_DEVSTAT_ACKNOWLEDGE        = 1,
    VIRTIO_DEVSTAT_DRIVER             = 2,
    VIRTIO_DEVSTAT_DRIVER_OK          = 4,
    VIRTIO_DEVSTAT_FEATURES_OK        = 8,
    VIRTIO_DEVSTAT_DEVICE_NEEDS_RESET = 64,
    VIRTIO_DEVSTAT_FAILED             = 128,
};

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

// TODO: go back through and add all the missing conversions from LE32 to CPU

void virtio_input_queue_prepare_clip(virtio_device_input_queue_singletons_t *queue) {
    assert(queue != NULL && queue->prepare_mut != NULL && queue->used != NULL);
    queue->prepare_mut->new_used_idx = letoh16(atomic_load(queue->used->idx));
}

void virtio_input_queue_advance_clip(virtio_device_input_queue_replica_t *queue) {
    assert(queue != NULL && queue->prepare_mut != NULL && queue->io_duct != NULL && queue->used != NULL);

    uint16_t last_used_idx = 0;
    duct_txn_t txn;
    duct_receive_prepare(&txn, queue->mut_duct, queue->replica_id);
    if (duct_receive_message(&txn, &last_used_idx, NULL) != sizeof(last_used_idx)) {
        last_used_idx = letoh16(atomic_load(queue->used->idx));
        debugf(WARNING, "Failed to feed forward any value for last_used_idx; falling back to current used index %u.",
               last_used_idx);
    }
    duct_receive_commit(&txn);

    uint16_t new_used_idx = letoh16(atomic_load(queue->used->idx));
    uint16_t descriptor_count = (uint16_t) (new_used_idx - last_used_idx);
    // this is to prevent skew caused by queue->used->idx changing between the first and last advance clip.
    uint16_t alt_descriptor_count = (uint16_t) (queue->prepare_mut->new_used_idx - last_used_idx);
    if (descriptor_count > alt_descriptor_count) {
        // We don't mark this as a warning, because it naturally occurs as a result of keep-alive messages being sent
        // at random intervals.
        debugf(DEBUG, "Locally computed descriptor count (%u) exceeds prepared descriptor (%u); reverting.",
               descriptor_count, alt_descriptor_count);
        descriptor_count = alt_descriptor_count;
    } else if (descriptor_count < alt_descriptor_count) {
        debugf(WARNING, "Prepared descriptor count (%u) exceeds locally computed descriptor (%u); ignoring.",
               alt_descriptor_count, descriptor_count);
    }
    assert(descriptor_count <= queue->queue_num);

#ifdef DEBUG_VIRTQ
    debugf(TRACE, "Advance clip [%u] for input queue %u: received descriptor count is %u.",
           queue->replica_id, queue->queue_index, descriptor_count);
#endif

    local_time_t timestamp = timer_epoch_ns();

    duct_send_prepare(&txn, queue->io_duct, queue->replica_id);

    assert(queue->message_size == duct_message_size(queue->io_duct));
    uint8_t *merge_buffer = queue->merge_buffer; // populate iff (queue->queue_num != duct_max_flow(queue->io_duct))
                                                 // TODO: ensure merge_buffer is of size message_size
    size_t merge_offset = 0;
    for (uint16_t i = 0; i < descriptor_count; i++) {
        // process descriptor
        uint32_t ring_index = (last_used_idx + i) % queue->queue_num;
        struct virtq_used_elem *elem = &queue->used->ring[ring_index];
        assert(ring_index == elem->id);
        assert(elem->len > 0 && elem->len <= queue->message_size);
        const uint8_t *elem_data = &queue->receive_buffer[ring_index * queue->message_size];
        if (merge_buffer == NULL) {
            // if merging is disabled, then transmit once for each descriptor
            duct_send_message(&txn, elem_data, elem->len, timestamp);
            continue;
        }
        // if merging is enabled, then collect data into a buffer until it's large enough to send.
        assert(merge_offset < queue->message_size);
        size_t merge_step_length = queue->message_size - merge_offset;
        if (merge_step_length > elem->len) {
            merge_step_length = elem->len;
        }
        memcpy(merge_buffer + merge_offset, elem_data, merge_step_length);
        merge_offset += merge_step_length;
        assert(merge_offset <= queue->message_size);
        if (merge_offset == queue->message_size) {
            if (duct_send_allowed(&txn)) {
#ifdef DEBUG_VIRTQ
                debugf(TRACE, "VIRTIO queue with merge enabled transmitted %u bytes.", merge_offset);
#endif
                duct_send_message(&txn, merge_buffer, merge_offset, timestamp);
            } else {
                debugf(WARNING, "VIRTIO queue with merge enabled discarded %u bytes.", merge_offset);
            }
            merge_buffer = NULL;
            merge_offset = 0;
        }
        if (merge_step_length < elem->len) {
            assert(merge_offset == 0);
            merge_offset = elem->len - merge_step_length;
            memcpy(merge_buffer, elem_data, merge_offset);
        }
    }

    if (merge_buffer != NULL && merge_offset > 0) {
        if (duct_send_allowed(&txn)) {
#ifdef DEBUG_VIRTQ
            debugf(TRACE, "VIRTIO queue with merge enabled transmitted %u bytes.", merge_offset);
#endif
            duct_send_message(&txn, merge_buffer, merge_offset, timestamp);
        } else {
            debugf(WARNING, "VIRTIO queue with merge enabled discarded %u bytes.", merge_offset);
        }
    }

    duct_send_commit(&txn);

    last_used_idx += descriptor_count;

    duct_send_prepare(&txn, queue->mut_duct, queue->replica_id);
    duct_send_message(&txn, &last_used_idx, sizeof(last_used_idx), 0);
    duct_send_commit(&txn);
}

void virtio_input_queue_commit_clip(virtio_device_input_queue_singletons_t *queue) {
    assert(queue != NULL && queue->avail != NULL && queue->desc != NULL && queue->mut_duct != NULL);
    // populate or repair all descriptors
    for (uint32_t i = 0; i < queue->queue_num; i++) {
        queue->avail->ring[i] = i; // TODO: is this redundant with other code?
        queue->desc[i] = (struct virtq_desc) {
            /* address (guest-physical) */
            .addr  = (uint64_t) (uintptr_t) &queue->receive_buffer[i * queue->message_size],
            .len   = queue->message_size,
            .flags = VIRTQ_DESC_F_WRITE,
            .next  = 0xFFFF, /* invalid index */
        };
    }

    duct_txn_t txn;
    uint16_t last_used_idx = 0;
    duct_receive_prepare(&txn, queue->mut_duct, VIRTIO_INPUT_QUEUE_REPLICAS /* n+1 index to siphon off the duct */);
    if (duct_receive_message(&txn, &last_used_idx, NULL) == sizeof(last_used_idx)) {
        uint16_t new_avail_idx = last_used_idx + queue->queue_num;
        if (new_avail_idx != queue->avail->idx) {
            atomic_store(queue->avail->idx, new_avail_idx);
            if (!atomic_load(queue->avail->flags)) {
                atomic_store_relaxed(queue->parent_device->mmio->queue_notify, queue->queue_index);
            }
        }
    } else {
        debugf(WARNING, "Failed to retrieve any value for last_used_idx; not updating avail index.");
    }
    duct_receive_commit(&txn);
}

void virtio_output_queue_monitor_clip(virtio_device_output_queue_t *queue) {
    assert(queue != NULL && queue->duct != NULL && queue->parent_device != NULL);
    assert(queue->desc != NULL && queue->avail != NULL && queue->used != NULL);

    uint16_t new_used_idx = letoh16(atomic_load(queue->used->idx));
    uint16_t descriptor_count = (uint16_t) (new_used_idx - queue->mut->last_used_idx);
    uint16_t new_avail_idx;

    // make sure that all data was written out successfully.
    queue->mut->last_used_idx += descriptor_count;
    assertf(queue->mut->last_used_idx == queue->avail->idx,
            "mismatch on queue=%u: used->idx=%u, last_used_idx=%u but avail->idx=%u",
            queue->queue_index, atomic_load(queue->used->idx), queue->mut->last_used_idx, queue->avail->idx);
    // (note: we *could* consider validating that the lengths are zero, and that the IDs match the ring indexes.)
    duct_txn_t txn;
    duct_receive_prepare(&txn, queue->duct, REPLICA_ID);
    // write out all messages
    uint16_t msg_index;
    for (msg_index = 0; msg_index < queue->queue_num; msg_index++) {
        uint16_t ring_index = (queue->mut->last_used_idx + msg_index) % queue->queue_num;
        uint8_t *buffer = &queue->buffer[ring_index * duct_message_size(queue->duct)];
        size_t size = 0;
        if (!(size = duct_receive_message(&txn, buffer, NULL))) {
            break;
        }
        assert(size >= 1 && size <= duct_message_size(queue->duct));
        // populate descriptor -- or repair any errors in it
        queue->desc[ring_index] = (struct virtq_desc) {
            /* address (guest-physical) */
            .addr  = (uint64_t) (uintptr_t) &queue->buffer[ring_index * duct_message_size(queue->duct)],
            .len   = size,
            .flags = 0,
            .next  = 0xFFFF, /* invalid index */
        };
    }
    if (duct_receive_message(&txn, NULL, NULL) > 0) {
        abortf("should never receive more than the maximum flow in one clip execution");
    }
    duct_receive_commit(&txn);
    new_avail_idx = queue->mut->last_used_idx + msg_index;

    if (new_avail_idx != queue->avail->idx) {
        atomic_store(queue->avail->idx, new_avail_idx);
        if (!atomic_load(queue->avail->flags)) {
            atomic_store_relaxed(queue->parent_device->mmio->queue_notify, queue->queue_index);
        }
    }

    debugf(DEBUG, "New virtq avail index for %u, last_used_idx: %u, %u",
           queue->queue_index, queue->avail->idx, queue->mut->last_used_idx);
}

void virtio_device_setup_queue_internal(struct virtio_mmio_registers *mmio, uint32_t queue_index, size_t queue_num,
                                        struct virtq_desc *desc, struct virtq_avail *avail, struct virtq_used *used) {
    assert(mmio != NULL && queue_num > 0 && desc != NULL && avail != NULL && used != NULL);

    mmio->queue_sel = queue_index;
    if (mmio->queue_ready != 0) {
        abortf("VIRTIO device apparently already had virtqueue %d initialized; failing.", queue_index);
    }
    if (mmio->queue_num_max == 0) {
        abortf("VIRTIO device does not have queue %u that it was expected to have.", queue_index);
    }

    if (queue_num > mmio->queue_num_max) {
        abortf("VIRTIO device supports up to %u entries in a queue buffer, but max flow is %u.",
               mmio->queue_num_max, queue_num);
    }

    mmio->queue_num = queue_num;

    mmio->queue_desc   = (uint64_t) (uintptr_t) desc;
    mmio->queue_driver = (uint64_t) (uintptr_t) avail;
    mmio->queue_device = (uint64_t) (uintptr_t) used;

    atomic_store(mmio->queue_ready, 1);

    debugf(DEBUG, "VIRTIO queue %d now configured", queue_index);
}

void virtio_device_force_notify_queue(virtio_device_input_queue_singletons_t *queue) {
    assert(queue != NULL && queue->parent_device != NULL && queue->parent_device->mmio != NULL);

    // spuriously notify the queue.
    atomic_store_relaxed(queue->parent_device->mmio->queue_notify, queue->queue_index);
}

// this function runs during STAGE_RAW, so it had better not use any kernel registration facilities
void virtio_device_init_internal(virtio_device_t *device) {
    assert(device != NULL);
    struct virtio_mmio_registers *mmio = device->mmio;

    debugf(DEBUG, "VIRTIO device: addr=%x, irq=%u.", (uintptr_t) device->mmio, device->irq);

    if (le32toh(mmio->magic_value) != VIRTIO_MAGIC_VALUE) {
        abortf("VIRTIO device had the wrong magic number: 0x%08x instead of 0x%08x; failing.",
               le32toh(mmio->magic_value), VIRTIO_MAGIC_VALUE);
    }

    if (le32toh(mmio->version) == VIRTIO_LEGACY_VERSION) {
        abortf("VIRTIO device configured as legacy-only; cannot initialize; failing. "
               "Set -global virtio-mmio.force-legacy=false to fix this.");
    } else if (le32toh(mmio->version) != VIRTIO_VERSION) {
        abortf("VIRTIO device version not recognized: found %u instead of %u; failing.",
               le32toh(mmio->version), VIRTIO_VERSION);
    }

    // make sure this is a serial port
    if (le32toh(mmio->device_id) != device->expected_device_id) {
        abortf("VIRTIO device ID=%u instead of ID=%u; failing.", le32toh(mmio->device_id), device->expected_device_id);
    }

    // reset the device
    mmio->status = htole32(0);

    // acknowledge the device
    mmio->status |= htole32(VIRTIO_DEVSTAT_ACKNOWLEDGE);
    mmio->status |= htole32(VIRTIO_DEVSTAT_DRIVER);

    // read the feature bits
    mmio->device_features_sel = htole32(0);
    uint64_t features = htole32(mmio->device_features);
    mmio->device_features_sel = htole32(1);
    features |= ((uint64_t) htole32(mmio->device_features)) << 32;

    // select feature bits
    device->feature_select_cb(&features);

    // write selected bits back
    mmio->driver_features_sel = htole32(0);
    mmio->driver_features = htole32((uint32_t) features);
    mmio->driver_features_sel = htole32(1);
    mmio->driver_features = htole32((uint32_t) (features >> 32));

    // validate features
    mmio->status |= htole32(VIRTIO_DEVSTAT_FEATURES_OK);
    if (!(le32toh(mmio->status) & VIRTIO_DEVSTAT_FEATURES_OK)) {
        abortf("VIRTIO device did not set FEATURES_OK: read back status=%08x; failing.", mmio->status);
    }

    // enable driver
    mmio->status |= htole32(VIRTIO_DEVSTAT_DRIVER_OK);
}

void *virtio_device_config_space(virtio_device_t *device) {
    assert(device != NULL && device->mmio != NULL);
    return device->mmio + 1;
}
