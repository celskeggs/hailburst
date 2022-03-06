#include <endian.h>
#include <stddef.h>
#include <stdint.h>

#include <rtos/gic.h>
#include <rtos/virtio.h>
#include <hal/atomic.h>
#include <hal/clock.h>
#include <hal/debug.h>
#include <hal/thread.h>
#include <synch/io.h>

// #define DEBUG_VIRTQ

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

void virtio_queue_monitor_clip(struct virtio_device_queue *queue) {
    assert(queue != NULL && queue->duct != NULL && queue->parent_device != NULL);

    assert(queue->desc != NULL && queue->avail != NULL && queue->used != NULL);

    uint16_t new_used_idx = letoh16(atomic_load(queue->used->idx));
    uint16_t descriptor_count = (uint16_t) (new_used_idx - queue->last_used_idx);
    uint16_t new_avail_idx;
    if (queue->direction == QUEUE_OUTPUT) {
        // make sure that all data was written out successfully.
        queue->last_used_idx += descriptor_count;
        assert(queue->last_used_idx == queue->avail->idx);
        // (note: we *could* consider validating that the lengths are zero, and that the IDs match the ring indexes.)
        duct_txn_t txn;
        duct_receive_prepare(&txn, queue->duct, REPLICA_ID);
        // write out all messages
        uint16_t msg_index;
        for (msg_index = 0; msg_index < queue->queue_num; msg_index++) {
            uint16_t ring_index = (queue->last_used_idx + msg_index) % queue->queue_num;
            uint8_t *buffer = &queue->buffer[ring_index * duct_message_size(queue->duct)];
            size_t size = 0;
            if (!(size = duct_receive_message(&txn, buffer, NULL))) {
                break;
            }
            assert(size >= 1 && size <= duct_message_size(queue->duct));
            queue->desc[ring_index].len = size;
        }
        if (duct_receive_message(&txn, NULL, NULL) > 0) {
            abortf("should never receive more than the maximum flow in one clip execution");
        }
        duct_receive_commit(&txn);
        new_avail_idx = queue->last_used_idx + msg_index;
    } else if (queue->direction == QUEUE_INPUT) {
        debugf(TRACE, "Monitor clip for input queue %u: received descriptor count is %u.",
               queue->queue_index, descriptor_count);
        uint64_t timestamp = clock_timestamp();
        duct_txn_t txn;
        duct_send_prepare(&txn, queue->duct, REPLICA_ID);
        assert(descriptor_count <= queue->queue_num);
        bool allow_merge = (queue->queue_num) != duct_max_flow(queue->duct);
        uint8_t *current_buffer = NULL;
        size_t current_offset = 0;
        for (uint16_t i = 0; i < descriptor_count; i++) {
            // process descriptor
            uint32_t ring_index = (queue->last_used_idx + i) % queue->queue_num;
            struct virtq_used_elem *elem = &queue->used->ring[ring_index];
            assert(ring_index == elem->id);
            assert(elem->len > 0 && elem->len <= duct_message_size(queue->duct));
            uint8_t *elem_data = &queue->buffer[ring_index * duct_message_size(queue->duct)];
            if (!allow_merge) {
                // if merging is disabled, then transmit once for each descriptor
                duct_send_message(&txn, elem_data, elem->len, timestamp);
            } else {
                // if merging is enabled, then make sure we combine descriptor data
                if (current_buffer != NULL) {
                    assert(0 < current_offset && current_offset < duct_message_size(queue->duct));
                    size_t merge_count = duct_message_size(queue->duct) - current_offset;
                    if (merge_count > elem->len) {
                        merge_count = elem->len;
                    }
                    memcpy(current_buffer + current_offset, elem_data, merge_count);
                    current_offset += merge_count;
                    assert(current_offset <= duct_message_size(queue->duct));
                    if (current_offset == duct_message_size(queue->duct)) {
                        if (duct_send_allowed(&txn)) {
                            debugf(TRACE, "VIRTIO queue with merge enabled transmitted %u bytes.", current_offset);
                            duct_send_message(&txn, current_buffer, current_offset, timestamp);
                        } else {
                            debugf(WARNING, "VIRTIO queue with merge enabled discarded %u bytes.", current_offset);
                        }
                        current_buffer = NULL;
                        current_offset = 0;
                    }
                    if (merge_count < elem->len) {
                        assert(current_offset == 0 && current_buffer == NULL);
                        current_offset = elem->len - merge_count;
                        current_buffer = elem_data;
                        memmove(current_buffer, current_buffer + merge_count, current_offset);
                    }
                } else {
                    assert(current_offset == 0 && current_buffer == NULL);
                    if (elem->len == duct_message_size(queue->duct)) {
                        if (duct_send_allowed(&txn)) {
                            debugf(TRACE, "VIRTIO queue with merge enabled transmitted %u bytes.", current_offset);
                            duct_send_message(&txn, current_buffer, current_offset, timestamp);
                        } else {
                            debugf(WARNING, "VIRTIO queue with merge enabled discarded %u bytes.", current_offset);
                        }
                    } else {
                        current_offset = elem->len;
                        current_buffer = elem_data;
                    }
                }
            }

            // prepare descriptor
            assert(queue->avail->ring[ring_index] == ring_index);
        }

        if (allow_merge && current_buffer != NULL) {
            assert(current_offset > 0);
            if (duct_send_allowed(&txn)) {
                debugf(TRACE, "VIRTIO queue with merge enabled transmitted %u bytes.", current_offset);
                duct_send_message(&txn, current_buffer, current_offset, timestamp);
            } else {
                debugf(WARNING, "VIRTIO queue with merge enabled discarded %u bytes.", current_offset);
            }
        }
        duct_send_commit(&txn);
        queue->last_used_idx += descriptor_count;
        new_avail_idx = queue->last_used_idx + queue->queue_num;
    } else {
        abortf("invalid direction for queue");
    }

    if (new_avail_idx != queue->avail->idx) {
        atomic_store(queue->avail->idx, new_avail_idx);
        if (!atomic_load(queue->avail->flags)) {
            atomic_store_relaxed(queue->parent_device->mmio->queue_notify, queue->queue_index);
        }
    }
}

static void virtio_device_irq_callback(void *opaque_device) {
    struct virtio_device *device = (struct virtio_device *) opaque_device;
    assert(device != NULL && device->initialized == true);

    uint32_t status = device->mmio->interrupt_status;
    if (status & VIRTIO_IRQ_BIT_USED_BUFFER) {
        // TODO: do we need to notify ANYTHING here?
    }
    device->mmio->interrupt_ack = status;
}

void virtio_device_setup_queue_internal(struct virtio_device_queue *queue) {
    assert(queue != NULL);
    assert(queue->parent_device != NULL);
    assert(queue->duct != NULL);
    assert(queue->queue_num > 0);
    assert(queue->desc != NULL);
    assert(queue->avail != NULL);
    assert(queue->used != NULL);

    struct virtio_mmio_registers *mmio = queue->parent_device->mmio;
    assert(mmio != NULL);

    mmio->queue_sel = queue->queue_index;
    if (mmio->queue_ready != 0) {
        abortf("VIRTIO device apparently already had virtqueue %d initialized; failing.", queue->queue_index);
    }
    // inconsistency if we hit this: we already checked this condition during discovery!
    assert(mmio->queue_num_max != 0);

    if (queue->queue_num > mmio->queue_num_max) {
        abortf("VIRTIO device supports up to %u entries in a queue buffer, but max flow is %u.",
               mmio->queue_num_max, queue->queue_num);
    }

    mmio->queue_num = queue->queue_num;

    queue->last_used_idx = 0;

    mmio->queue_desc   = (uint64_t) (uintptr_t) queue->desc;
    mmio->queue_driver = (uint64_t) (uintptr_t) queue->avail;
    mmio->queue_device = (uint64_t) (uintptr_t) queue->used;

    atomic_store(mmio->queue_ready, 1);

    // configure descriptors to refer to chart memory directly
    for (uint32_t i = 0; i < queue->queue_num; i++) {
        queue->desc[i] = (struct virtq_desc) {
            /* address (guest-physical) */
            .addr  = (uint64_t) (uintptr_t) &queue->buffer[i * duct_message_size(queue->duct)],
            .len   = queue->direction == QUEUE_INPUT ? duct_message_size(queue->duct) : 0,
            .flags = queue->direction == QUEUE_INPUT ? VIRTQ_DESC_F_WRITE : 0,
            .next  = 0xFFFF /* invalid index */,
        };
    }

    if (queue->direction == QUEUE_INPUT) {
        assert(queue->avail->idx == queue->queue_num);
        if (!atomic_load(queue->avail->flags)) {
            atomic_store_relaxed(mmio->queue_notify, queue->queue_index);
        }
    }

    debugf(DEBUG, "VIRTIO queue %d now configured", queue->queue_index);
}

void virtio_device_force_notify_queue(struct virtio_device_queue *queue) {
    // validate device initialized
    assert(queue != NULL && queue->parent_device != NULL && queue->parent_device->initialized == true);
    // validate queue index
    assert(queue->queue_index < queue->parent_device->num_queues);
    // make sure this queue has actually been set up.
    assert(queue->duct != NULL);

    // spuriously notify the queue.
    atomic_store_relaxed(queue->parent_device->mmio->queue_notify, queue->queue_index);
}

// this function runs during STAGE_RAW, so it had better not use any kernel registration facilities
void virtio_device_init_internal(struct virtio_device *device) {
    assert(device != NULL && device->initialized == false && device->num_queues == 0);
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

    // discover number of queues
    for (uint32_t queue_i = 0; ; queue_i++) {
        device->mmio->queue_sel = queue_i;
        if (device->mmio->queue_ready != 0) {
            abortf("VIRTIO device already had virtqueue %d initialized; failing.", queue_i);
        }
        if (device->mmio->queue_num_max == 0) {
            device->num_queues = queue_i;
            break;
        }
    }

    if (device->num_queues == 0) {
        abortf("VIRTIO device discovered to have 0 queues; failing.");
    }

    debugf(DEBUG, "VIRTIO device discovered to have %u queues.", device->num_queues);

    // enable driver
    mmio->status |= htole32(VIRTIO_DEVSTAT_DRIVER_OK);

    assert(device->initialized == false);
    device->initialized = true;
}

void virtio_device_start_internal(struct virtio_device *device) {
    assert(device != NULL && device->initialized == true);
    // it's okay to run this here, even before the task is necessarily created, because interrupts will not be enabled
    // until the scheduler starts running
    enable_irq(device->irq, virtio_device_irq_callback, device);
}

void *virtio_device_config_space(struct virtio_device *device) {
    assert(device != NULL && device->initialized && device->mmio != NULL);
    return device->mmio + 1;
}
