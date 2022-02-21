#include <string.h>

#include <hal/clock.h>
#include <synch/duct.h>

//#define DUCT_DEBUG

void duct_send_prepare(duct_t *duct, uint8_t sender_id) {
    assert(duct != NULL);
    assert(sender_id < duct->sender_replicas);

#ifdef DUCT_DEBUG
    debugf(TRACE, "duct send prepare: %p[%u]", duct, sender_id);
#endif

    eplock_acquire(duct->mutex); /* hold lock until we commit */

    /* ensure that all previously-sent flows have been consumed */
    for (uint8_t receiver_id = 0; receiver_id < duct->receiver_replicas; receiver_id++) {
        if (duct->flow_status[sender_id * duct->receiver_replicas + receiver_id] != DUCT_MISSING_FLOW) {
            abortf("Temporal ordering broken: previous duct receiver did not act on schedule. (sender=%s)",
                   task_get_name(task_get_current()));
        }
    }

    /* reset count to zero */
    duct->flow_current = 0;
}

// returns true if we're allowed to send at least one more message
bool duct_send_allowed(duct_t *duct, uint8_t sender_id) {
    assert(duct != NULL);
    assert(sender_id < duct->sender_replicas);
    assert(eplock_held(duct->mutex));

    assert(duct->flow_current <= duct->max_flow);
    return duct->flow_current < duct->max_flow;
}

// asserts if we've used up our max flow in this transaction already
void duct_send_message(duct_t *duct, uint8_t sender_id, void *message, size_t size, uint64_t timestamp) {
    assert(duct != NULL);
    assert(sender_id < duct->sender_replicas);
    assert(eplock_held(duct->mutex));
    assert(message != NULL && size >= 1 && size <= duct->message_size);

    assert(duct->flow_current < duct->max_flow);

    /* copy message into the transit queue */
    duct_message_t *entry = duct_lookup_message(duct, sender_id, duct->flow_current);
    entry->size = size;
    entry->timestamp = timestamp;
    memcpy(entry->body, message, size);
    // NOTE: this memset is too slow to be allowable!
    // memset(entry->body + size, 0, duct->message_size - size);

    duct->flow_current += 1;
}

void duct_send_commit(duct_t *duct, uint8_t sender_id) {
    assert(duct != NULL);
    assert(sender_id < duct->sender_replicas);
    assert(eplock_held(duct->mutex));

    assert(duct->flow_current <= duct->max_flow);

    /* emit new counts */
    for (uint8_t receiver_id = 0; receiver_id < duct->receiver_replicas; receiver_id++) {
        assert(duct->flow_status[sender_id * duct->receiver_replicas + receiver_id] == DUCT_MISSING_FLOW);
        duct->flow_status[sender_id * duct->receiver_replicas + receiver_id] = duct->flow_current;
    }

    /* clear scratch variable */
    duct->flow_current = DUCT_MISSING_FLOW;

    eplock_release(duct->mutex);

#ifdef DUCT_DEBUG
    debugf(TRACE, "duct send commit: %p[%u]", duct, sender_id);
#endif
}

void duct_receive_prepare(duct_t *duct, uint8_t receiver_id) {
    assert(duct != NULL);
    assert(receiver_id < duct->receiver_replicas);

#ifdef DUCT_DEBUG
    debugf(TRACE, "duct receive prepare: %p[%u]", duct, receiver_id);
#endif

    eplock_acquire(duct->mutex); /* hold lock until we commit */

    /* ensure that all senders have transmitted flows for us */
    for (uint8_t sender_id = 0; sender_id < duct->sender_replicas; sender_id++) {
        if (duct->flow_status[sender_id * duct->receiver_replicas + receiver_id] == DUCT_MISSING_FLOW) {
            abortf("Temporal ordering broken: previous duct sender did not act on schedule. (receiver=%s)",
                   task_get_name(task_get_current()));
        }
        assert(duct->flow_status[sender_id * duct->receiver_replicas + receiver_id] <= duct->max_flow);
    }

    /* reset index to zero */
    duct->flow_current = 0;
}

// returns size > 0 if a message was successfully received. if size = 0, then we're done with this transaction.
size_t duct_receive_message(duct_t *duct, uint8_t receiver_id, void *message_out, uint64_t *timestamp_out) {
    assert(duct != NULL);
    assert(receiver_id < duct->receiver_replicas);
    assert(eplock_held(duct->mutex));

    assert(duct->flow_current <= duct->max_flow);
    if (duct->flow_current == duct->max_flow) {
        /* indicate that we've read the maximum number of messages */
        return 0;
    }

    /* ensure that all senders agree on whether they have another message to send us */
    duct_flow_index another_count = 0;
    for (uint8_t sender_id = 0; sender_id < duct->sender_replicas; sender_id++) {
        duct_flow_index index = duct->flow_status[sender_id * duct->receiver_replicas + receiver_id];
        assert(index != DUCT_MISSING_FLOW && index <= duct->max_flow);
        if (index > duct->flow_current) {
            another_count++;
        }
    }

    // TODO: change this when we move to non-strict mode
    assert(another_count == 0 || another_count == duct->sender_replicas);
    if (another_count == 0) {
        /* indicate that we've read all the messages that were sent */
        return 0;
    }

    /* grab the first message as a comparison point */
    duct_message_t *first_message = duct_lookup_message(duct, 0, duct->flow_current);
    assert(first_message != NULL);
    size_t first_size = first_message->size;
    uint64_t first_timestamp = first_message->timestamp;
    assert(first_size >= 1 && first_size <= duct->message_size);
    void *message_ref;
    if (message_out != NULL) {
        memcpy(message_out, first_message->body, first_size);
        message_ref = message_out;
    } else {
        message_ref = first_message->body;
    }

    /* ensure the other messages match */
    for (uint8_t sender_id = 1; sender_id < duct->sender_replicas; sender_id++) {
        duct_message_t *next_message = duct_lookup_message(duct, sender_id, duct->flow_current);
        assert(next_message != NULL);
        assert(next_message->size == first_size);
        assert(next_message->timestamp == first_timestamp);
        assert(0 == memcmp(message_ref, next_message->body, first_size));
    }

    if (timestamp_out) {
        *timestamp_out = first_timestamp;
    }

    duct->flow_current += 1;

    return first_size;
}

// asserts if we left any messages unprocessed
void duct_receive_commit(duct_t *duct, uint8_t receiver_id) {
    assert(duct != NULL);
    assert(receiver_id < duct->receiver_replicas);
    assert(eplock_held(duct->mutex));

    assert(duct->flow_current <= duct->max_flow);

    /* ensure that counts match actual number processed, and mark everything as consumed. */
    for (uint8_t sender_id = 0; sender_id < duct->sender_replicas; sender_id++) {
        uint32_t index = sender_id * duct->receiver_replicas + receiver_id;
        assertf(duct->flow_status[index] == duct->flow_current,
                "flow_status=%u, flow_current=%u", duct->flow_status[index], duct->flow_current);
        duct->flow_status[index] = DUCT_MISSING_FLOW;
    }

    /* clear scratch variable */
    duct->flow_current = DUCT_MISSING_FLOW;

    eplock_release(duct->mutex);

#ifdef DUCT_DEBUG
    debugf(TRACE, "duct receive commit: %p[%u]", duct, receiver_id);
#endif
}
