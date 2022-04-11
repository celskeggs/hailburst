#include <rtos/virtio.h>

enum {
    REPLICA_PREPARE_ID = 0,
    REPLICA_COMMIT_ID = 1,
};

void virtio_output_queue_prepare_clip(virtio_device_output_queue_t *queue) {
    assert(queue != NULL && queue->duct != NULL);
    assert(queue->desc != NULL && queue->avail != NULL && queue->used != NULL);
    assert(queue->message_size == duct_message_size(queue->duct));

    uint16_t new_used_idx = le16toh(atomic_load(queue->used->idx));

    uint16_t avail = le16toh(queue->avail->idx);
    if (new_used_idx != avail) {
        // TODO: is this the right way to address this condition?
        debugf(WARNING, "Mismatch on output queue %u: used->idx=%u, but avail->idx=%u; clearing.",
               queue->queue_index, new_used_idx, le16toh(queue->avail->idx));
    }

    // prepare phase: read in data and stick it into the transmit buffer; then populate descriptors!
    duct_txn_t txn;
    duct_receive_prepare(&txn, queue->duct, REPLICA_PREPARE_ID);

    for (uint16_t msg_index = 0; msg_index < queue->queue_num; msg_index++) {
        uint16_t ring_index = (avail + msg_index) % queue->queue_num;
        uint8_t *transmit_buffer = &queue->transmit_buffer[ring_index * queue->message_size];
        size_t length = 0;
        if (!(length = duct_receive_message(&txn, transmit_buffer, NULL))) {
            break;
        }
        assert(length >= 1 && length <= queue->message_size);
        // populate descriptor -- or repair any errors in it
        queue->desc[ring_index] = (struct virtq_desc) {
            /* address (guest-physical) */
            .addr  = htole64((uint64_t) (uintptr_t) transmit_buffer),
            .len   = htole32(length),
            .flags = htole16(0),
            .next  = htole16(0xFFFF), /* invalid index */
        };
        queue->avail->ring[ring_index] = htole16(ring_index); // TODO: is this redundant with other code?
    }

    duct_receive_commit(&txn);

    // no actual transmission here; that's the job of the commit clip!
}

void virtio_output_queue_commit_clip(virtio_device_output_queue_t *queue) {
    assert(queue != NULL && queue->duct != NULL && queue->parent_device != NULL);
    assert(queue->desc != NULL && queue->avail != NULL && queue->used != NULL);

    uint16_t avail = le16toh(queue->avail->idx);

    // commit phase: compare the data in the transmit buffer to what it should be, and then decide which messages to
    // transmit at this time!
    duct_txn_t txn;
    duct_receive_prepare(&txn, queue->duct, REPLICA_COMMIT_ID);

    uint16_t msg_index;
    for (msg_index = 0; msg_index < queue->queue_num; msg_index++) {
        uint16_t ring_index = (avail + msg_index) % queue->queue_num;
        const uint8_t *transmit_buffer = &queue->transmit_buffer[ring_index * queue->message_size];
        size_t length = 0;
        if (!(length = duct_receive_message(&txn, queue->compare_buffer, NULL))) {
            break;
        }
        assert(length >= 1 && length <= queue->message_size);
        const struct virtq_desc *compare_desc = &queue->desc[ring_index];
        if ((uintptr_t) le64toh(compare_desc->addr) != (uintptr_t) transmit_buffer
                || le32toh(compare_desc->len) != length
                || le16toh(compare_desc->flags) != 0
                || le16toh(compare_desc->next) != 0xFFFF) {
            debugf(WARNING, "Message at index %u on output queue %u had a mismatched descriptor; truncating.",
                   msg_index, queue->queue_index);
            break;
        }
        if (le16toh(queue->avail->ring[ring_index]) != ring_index) {
            debugf(WARNING, "Message at index %u on output queue %u had a broken avail ring entry; truncating.",
                   msg_index, queue->queue_index);
            break;
        }
        if (memcmp(transmit_buffer, queue->compare_buffer, length) != 0) {
            debugf(WARNING, "Message at index %u on output queue %u did not match transmission buffer; truncating.",
                   msg_index, queue->queue_index);
            break;
        }
    }

    duct_receive_commit(&txn);

    // now that we've validated which messages are available where the prepare clip did the right thing, transmit them!
    if (msg_index > 0) {
        atomic_store(queue->avail->idx, htole16(avail + msg_index));
        if (!le16toh(atomic_load(queue->avail->flags))) {
            atomic_store_relaxed(queue->parent_device->mmio->queue_notify, htole32(queue->queue_index));
        }
    }
}
