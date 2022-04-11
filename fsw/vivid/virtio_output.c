#include <endian.h>

#include <rtos/virtio.h>

enum {
    REPLICA_ID = 0,
};

// TODO: go back through and add all the missing conversions from LE32 to CPU

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
