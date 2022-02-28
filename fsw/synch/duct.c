#include <string.h>

#include <hal/atomic.h>
#include <hal/clock.h>
#include <hal/thread.h>
#include <synch/duct.h>

//#define DUCT_DEBUG

void duct_send_prepare(duct_txn_t *txn, duct_t *duct, uint8_t sender_id) {
    assert(txn != NULL && duct != NULL);
    assert(sender_id < duct->sender_replicas);

#ifdef DUCT_DEBUG
    debugf(TRACE, "duct send prepare: %p[%u]", duct, sender_id);
#endif

    txn->mode = DUCT_TXN_SEND;
    txn->duct = duct;
    txn->replica_id = sender_id;
    txn->flow_current = 0;

    /* ensure that all previously-sent flows have been consumed */
    for (uint8_t receiver_id = 0; receiver_id < duct->receiver_replicas; receiver_id++) {
        if (atomic_load(duct->flow_status[sender_id * duct->receiver_replicas + receiver_id]) != DUCT_MISSING_FLOW) {
            abortf("Temporal ordering broken: previous duct receiver did not act on schedule. (sender=%s)",
                   task_get_name(task_get_current()));
        }
    }
}

// returns true if we're allowed to send at least one more message
bool duct_send_allowed(duct_txn_t *txn) {
    assert(txn != NULL && txn->duct != NULL);
    assert(txn->mode == DUCT_TXN_SEND);
    assert(txn->replica_id < txn->duct->sender_replicas);
    assert(txn->flow_current <= txn->duct->max_flow);

    return txn->flow_current < txn->duct->max_flow;
}

// asserts if we've used up our max flow in this transaction already
void duct_send_message(duct_txn_t *txn, void *message, size_t size, uint64_t timestamp) {
    assert(txn != NULL && txn->duct != NULL);
    assert(txn->mode == DUCT_TXN_SEND);
    assert(txn->replica_id < txn->duct->sender_replicas);
    assert(txn->flow_current < txn->duct->max_flow);
    assert(message != NULL && size >= 1 && size <= txn->duct->message_size);

    /* copy message into the transit queue */
    duct_message_t *entry = duct_lookup_message(txn->duct, txn->replica_id, txn->flow_current);
    entry->size = size;
    entry->timestamp = timestamp;
    memcpy(entry->body, message, size);
    // NOTE: this memset is too slow to be allowable!
    // memset(entry->body + size, 0, duct->message_size - size);

    txn->flow_current += 1;
}

void duct_send_commit(duct_txn_t *txn) {
    assert(txn != NULL && txn->duct != NULL);
    assert(txn->mode == DUCT_TXN_SEND);
    assert(txn->replica_id < txn->duct->sender_replicas);
    assert(txn->flow_current <= txn->duct->max_flow);

    /* emit new counts */
    for (uint8_t receiver_id = 0; receiver_id < txn->duct->receiver_replicas; receiver_id++) {
        assert(txn->duct->flow_status[txn->replica_id * txn->duct->receiver_replicas + receiver_id]
                    == DUCT_MISSING_FLOW);
        atomic_store(txn->duct->flow_status[txn->replica_id * txn->duct->receiver_replicas + receiver_id],
                     txn->flow_current);
    }

    /* clear transaction */
    txn->mode = DUCT_TXN_INVALID;
    txn->flow_current = DUCT_MISSING_FLOW;

#ifdef DUCT_DEBUG
    debugf(TRACE, "duct send commit: %p[%u]", txn->duct, txn->replica_id);
#endif
}

void duct_receive_prepare(duct_txn_t *txn, duct_t *duct, uint8_t receiver_id) {
    assert(txn != NULL && duct != NULL);
    assert(receiver_id < duct->receiver_replicas);

#ifdef DUCT_DEBUG
    debugf(TRACE, "duct receive prepare: %p[%u]", duct, receiver_id);
#endif

    txn->mode = DUCT_TXN_RECV;
    txn->duct = duct;
    txn->replica_id = receiver_id;
    txn->flow_current = 0;

    /* ensure that all senders have transmitted flows for us */
    for (uint8_t sender_id = 0; sender_id < duct->sender_replicas; sender_id++) {
        duct_flow_index status = atomic_load(duct->flow_status[sender_id * duct->receiver_replicas + receiver_id]);
        if (status == DUCT_MISSING_FLOW) {
            abortf("Temporal ordering broken: previous duct sender did not act on schedule. (receiver=%s)",
                   task_get_name(task_get_current()));
        }
        assert(status <= duct->max_flow);
    }
}

// returns size > 0 if a message was successfully received. if size = 0, then we're done with this transaction.
size_t duct_receive_message(duct_txn_t *txn, void *message_out, uint64_t *timestamp_out) {
    assert(txn != NULL && txn->duct != NULL);
    assert(txn->mode == DUCT_TXN_RECV);
    assert(txn->replica_id < txn->duct->receiver_replicas);
    assert(txn->flow_current <= txn->duct->max_flow);

    if (txn->flow_current == txn->duct->max_flow) {
        /* indicate that we've read the maximum number of messages */
        return 0;
    }

    /* ensure that all senders agree on whether they have another message to send us */
    duct_flow_index another_count = 0;
    for (uint8_t sender_id = 0; sender_id < txn->duct->sender_replicas; sender_id++) {
        duct_flow_index index =
                atomic_load(txn->duct->flow_status[sender_id * txn->duct->receiver_replicas + txn->replica_id]);
        assert(index != DUCT_MISSING_FLOW && index <= txn->duct->max_flow);
        if (index > txn->flow_current) {
            another_count++;
        }
    }

    // TODO: change this when we move to non-strict mode
    assert(another_count == 0 || another_count == txn->duct->sender_replicas);
    if (another_count == 0) {
        /* indicate that we've read all the messages that were sent */
        return 0;
    }

    /* grab the first message as a comparison point */
    duct_message_t *first_message = duct_lookup_message(txn->duct, 0, txn->flow_current);
    assert(first_message != NULL);
    size_t first_size = first_message->size;
    uint64_t first_timestamp = first_message->timestamp;
    assert(first_size >= 1 && first_size <= txn->duct->message_size);
    void *message_ref;
    if (message_out != NULL) {
        memcpy(message_out, first_message->body, first_size);
        message_ref = message_out;
    } else {
        message_ref = first_message->body;
    }

    /* ensure the other messages match */
    for (uint8_t sender_id = 1; sender_id < txn->duct->sender_replicas; sender_id++) {
        duct_message_t *next_message = duct_lookup_message(txn->duct, sender_id, txn->flow_current);
        assert(next_message != NULL);
        assert(next_message->size == first_size);
        assert(next_message->timestamp == first_timestamp);
        assert(0 == memcmp(message_ref, next_message->body, first_size));
    }

    if (timestamp_out) {
        *timestamp_out = first_timestamp;
    }

    txn->flow_current += 1;

    return first_size;
}

// asserts if we left any messages unprocessed
void duct_receive_commit(duct_txn_t *txn) {
    assert(txn != NULL && txn->duct != NULL);
    assert(txn->mode == DUCT_TXN_RECV);
    assert(txn->replica_id < txn->duct->receiver_replicas);
    assert(txn->flow_current <= txn->duct->max_flow);

    /* ensure that counts match actual number processed, and mark everything as consumed. */
    for (uint8_t sender_id = 0; sender_id < txn->duct->sender_replicas; sender_id++) {
        uint32_t status = txn->duct->flow_status[sender_id * txn->duct->receiver_replicas + txn->replica_id];
        assertf(status == txn->flow_current, "flow_status=%u, flow_current=%u", status, txn->flow_current);
        atomic_store(txn->duct->flow_status[sender_id * txn->duct->receiver_replicas + txn->replica_id],
                     DUCT_MISSING_FLOW);
    }

    /* clear transaction */
    txn->mode = DUCT_TXN_INVALID;
    txn->flow_current = DUCT_MISSING_FLOW;

#ifdef DUCT_DEBUG
    debugf(TRACE, "duct receive commit: %p[%u]", txn->duct, txn->replica_id);
#endif
}
