#include <rtos/virtio.h>

//#define DEBUG_VIRTQ

void virtio_input_queue_prepare_clip(virtio_device_input_queue_singletons_t *queue) {
    assert(queue != NULL && queue->prepare_mut != NULL && queue->used != NULL);
    queue->prepare_mut->new_used_idx = letoh16(atomic_load(queue->used->idx));
}

void virtio_input_queue_advance_clip(virtio_device_input_queue_replica_t *queue) {
    assert(queue != NULL && queue->prepare_mut != NULL && queue->io_duct != NULL && queue->used != NULL);

    bool mut_valid = false;
    struct virtio_device_input_queue_notepad_mutable *mut = notepad_feedforward(queue->mut_replica, &mut_valid);
    if (!mut_valid) {
        mut->last_used_idx = letoh16(atomic_load(queue->used->idx));
        debugf(WARNING, "Failed to feed forward any value for last_used_idx; falling back to current used index %u.",
               mut->last_used_idx);
    }

    uint16_t new_used_idx = letoh16(atomic_load(queue->used->idx));
    uint16_t descriptor_count = (uint16_t) (new_used_idx - mut->last_used_idx);
    // this is to prevent skew caused by queue->used->idx changing between the first and last advance clip.
    uint16_t alt_descriptor_count = (uint16_t) (queue->prepare_mut->new_used_idx - mut->last_used_idx);
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

    duct_txn_t txn;
    duct_send_prepare(&txn, queue->io_duct, queue->replica_id);

    assert(queue->message_size == duct_message_size(queue->io_duct));
    uint8_t *merge_buffer = queue->merge_buffer; // populate iff (queue->queue_num != duct_max_flow(queue->io_duct))
                                                 // TODO: ensure merge_buffer is of size message_size
    size_t merge_offset = 0;
    for (uint16_t i = 0; i < descriptor_count; i++) {
        // process descriptor
        uint32_t ring_index = (mut->last_used_idx + i) % queue->queue_num;
        struct virtq_used_elem *elem = &queue->used->ring[ring_index];
        assert(ring_index == le32toh(elem->id));
        size_t elem_len = le32toh(elem->len);
        assert(elem_len > 0 && elem_len <= queue->message_size);
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

    mut->last_used_idx += descriptor_count;
}

void virtio_input_queue_commit_clip(virtio_device_input_queue_singletons_t *queue) {
    assert(queue != NULL && queue->avail != NULL && queue->desc != NULL && queue->mut_observer != NULL);
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

    struct virtio_device_input_queue_notepad_mutable state;
    if (notepad_observe(queue->mut_observer, &state)) {
        uint16_t new_avail_idx = state.last_used_idx + queue->queue_num;
        if (new_avail_idx != le16toh(queue->avail->idx)) {
            atomic_store(queue->avail->idx, htole16(new_avail_idx));
            if (!le16toh(atomic_load(queue->avail->flags))) {
                atomic_store_relaxed(queue->parent_device->mmio->queue_notify, htole32(queue->queue_index));
            }
        }
    } else {
        debugf(WARNING, "Failed to retrieve a valid value for last_used_idx; not updating avail index.");
    }
}

void virtio_device_force_notify_queue(virtio_device_input_queue_singletons_t *queue) {
    assert(queue != NULL && queue->parent_device != NULL && queue->parent_device->mmio != NULL);

    // spuriously notify the queue.
    atomic_store_relaxed(queue->parent_device->mmio->queue_notify, htole32(queue->queue_index));
}
