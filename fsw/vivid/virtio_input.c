#include <rtos/virtio.h>
#include <hal/atomic.h>
#include <hal/clip.h>

#define DEBUG_VIRTQ

enum {
    REPLICA_PREPARE_ID = 0,
    REPLICA_COMMIT_ID = 1,
};

static void virtio_input_queue_common_data(virtio_device_input_queue_singletons_t *queue,
                                           uint8_t replica_id, uint16_t last_used_idx, uint16_t descriptor_count) {
    local_time_t timestamp = timer_epoch_ns();

    duct_txn_t txn;
    duct_send_prepare(&txn, queue->io_duct, replica_id);

    assert(queue->message_size == duct_message_size(queue->io_duct));
    uint8_t *merge_buffer = queue->merge_buffer; // populate iff (queue->queue_num != duct_max_flow(queue->io_duct))
                                                 // TODO: ensure merge_buffer is of size message_size
    size_t merge_offset = 0;
    for (uint16_t i = 0; i < descriptor_count; i++) {
        // process descriptor
        uint32_t ring_index = (last_used_idx + i) % queue->queue_num;
        struct virtq_used_elem *elem = &queue->used->ring[ring_index];
        assert(ring_index == le32toh(elem->id));
        size_t elem_len = le32toh(elem->len);
        assertf(elem_len > 0 && elem_len <= queue->message_size, "invalid elem_len = %u", elem_len);
        const uint8_t *elem_data = &queue->receive_buffer[ring_index * queue->message_size];
        if (merge_buffer == NULL) {
            // if merging is disabled, then transmit once for each descriptor
            duct_send_message(&txn, elem_data, elem_len, timestamp);
            continue;
        }
        // if merging is enabled, then collect data into a buffer until it's large enough to send.
        assert(merge_offset < queue->message_size);
        size_t merge_step_length = queue->message_size - merge_offset;
        if (merge_step_length > elem_len) {
            merge_step_length = elem_len;
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
        if (merge_step_length < elem_len) {
            assert(merge_offset == 0);
            merge_offset = elem_len - merge_step_length;
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
}

void virtio_input_queue_prepare_clip(virtio_device_input_queue_singletons_t *queue) {
    assert(queue != NULL && queue->used != NULL && queue->avail != NULL && queue->desc != NULL);

    uint16_t new_used_idx = letoh16(atomic_load(queue->used->idx));
    uint16_t last_used_idx = letoh16(atomic_load_relaxed(queue->avail->idx)) - queue->queue_num;
    uint16_t descriptor_count = (uint16_t) (new_used_idx - last_used_idx);
    assert(descriptor_count <= queue->queue_num);

    if (clip_is_restart()) {
        descriptor_count = 0;
    }

#ifdef DEBUG_VIRTQ
    debugf(TRACE, "Prepare clip for input queue %u: received descriptor count is %u.",
           queue->queue_index, descriptor_count);
#endif

    virtio_input_queue_common_data(queue, REPLICA_PREPARE_ID, last_used_idx, descriptor_count);

    queue->mut->last_descriptor_count = descriptor_count;

    // populate or repair all descriptors
    for (uint32_t i = 0; i < queue->queue_num; i++) {
        queue->avail->ring[i] = htole16(i); // TODO: is this redundant with other code?
        queue->desc[i] = (struct virtq_desc) {
            /* address (guest-physical) */
            .addr  = htole64((uint64_t) (uintptr_t) &queue->receive_buffer[i * queue->message_size]),
            .len   = htole32(queue->message_size),
            .flags = htole16(VIRTQ_DESC_F_WRITE),
            .next  = htole16(0xFFFF), /* invalid index */
        };
    }
}

void virtio_input_queue_commit_clip(virtio_device_input_queue_singletons_t *queue) {
    assert(queue != NULL && queue->used != NULL && queue->avail != NULL && queue->desc != NULL);

    uint16_t new_used_idx = letoh16(atomic_load(queue->used->idx));
    uint16_t last_used_idx = letoh16(atomic_load_relaxed(queue->avail->idx)) - queue->queue_num;
    uint16_t descriptor_count = (uint16_t) (new_used_idx - last_used_idx);
    assert(descriptor_count <= queue->queue_num);

    if (descriptor_count > queue->mut->last_descriptor_count) {
        descriptor_count = queue->mut->last_descriptor_count;
        // clear so that, if the prepare clip isn't getting scheduled, we don't try to advance.
        queue->mut->last_descriptor_count = 0;
    }

    if (clip_is_restart()) {
        descriptor_count = 0;
    }

    // validate that all descriptors are properly configured
    bool all_ok = true;
    for (uint32_t i = 0; i < queue->queue_num; i++) {
        if (letoh16(queue->avail->ring[i]) != i) {
            debugf(WARNING, "Commit clip for input queue %u: encountered invalid ring[%u] = %u != %u",
                            queue->queue_index, i, letoh16(queue->avail->ring[i]), i);
            all_ok = false;
            break;
        }
        if ((uintptr_t) letoh64(queue->desc[i].addr) != (uintptr_t) &queue->receive_buffer[i * queue->message_size]) {
            debugf(WARNING, "Commit clip for input queue %u: encountered invalid desc[%u].addr = %p != %p",
                            queue->queue_index, i, (void *) (uintptr_t) letoh64(queue->desc[i].addr),
                            (void *) (uintptr_t) &queue->receive_buffer[i * queue->message_size]);
            all_ok = false;
            break;
        }
        if (letoh32(queue->desc[i].len) != queue->message_size) {
            debugf(WARNING, "Commit clip for input queue %u: encountered invalid desc[%u].len = %u != %u",
                            queue->queue_index, i, letoh32(queue->desc[i].len), queue->message_size);
            all_ok = false;
            break;
        }
        if (letoh16(queue->desc[i].flags) != VIRTQ_DESC_F_WRITE) {
            debugf(WARNING, "Commit clip for input queue %u: encountered invalid desc[%u].flags = 0x%x != 0x%x",
                            queue->queue_index, i, letoh16(queue->desc[i].flags), VIRTQ_DESC_F_WRITE);
            all_ok = false;
            break;
        }
        if (letoh16(queue->desc[i].next) != 0xFFFF) {
            debugf(WARNING, "Commit clip for input queue %u: encountered invalid desc[%u].next = 0x%x != 0x%x",
                            queue->queue_index, i, letoh16(queue->desc[i].next), 0xFFFF);
            all_ok = false;
            break;
        }
    }
    if (!all_ok) {
        debugf(WARNING, "Commit clip for input queue %u: forced descriptor count to zero until descriptors "
                        "are repaired.", queue->queue_index);
        descriptor_count = 0;
    }

    virtio_input_queue_common_data(queue, REPLICA_COMMIT_ID, last_used_idx, descriptor_count);

    if (all_ok) {
        uint16_t new_avail_idx = new_used_idx + queue->queue_num;
        if (new_avail_idx != letoh16(queue->avail->idx)) {
            atomic_store(queue->avail->idx, htole16(new_avail_idx));
            if (!le16toh(atomic_load(queue->avail->flags))) {
                atomic_store_relaxed(queue->parent_device->mmio->queue_notify, htole32(queue->queue_index));
            }
        }
    }
}

void virtio_device_force_notify_queue(virtio_device_input_queue_notify_t *queue) {
    assert(queue != NULL && queue->parent_device != NULL && queue->parent_device->mmio != NULL);

    // spuriously notify the queue.
    atomic_store_relaxed(queue->parent_device->mmio->queue_notify, htole32(queue->queue_index));
}
