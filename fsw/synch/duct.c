#include <string.h>

#include <hal/atomic.h>
#include <hal/clip.h>
#include <hal/clock.h>
#include <hal/thread.h>
#include <synch/config.h>
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
            miscomparef("Temporal ordering broken: previous duct receiver did not act on schedule. (sender=%s)",
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

    // ensure that all receives run inside clips, so that if they get descheduled, they don't keep trying to interpret
    // received data that might be actively changing.
    clip_assert();

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
        if (status == DUCT_MISSING_FLOW || status > duct->max_flow) {
            miscomparef("Temporal ordering broken: previous duct sender did not act on schedule. (receiver=%s)",
                        task_get_name(task_get_current()));
            atomic_store_relaxed(duct->flow_status[sender_id * duct->receiver_replicas + receiver_id], 0);
        }
    }
}

static inline duct_flow_index majority_threshold(duct_t *duct) {
    return duct->sender_replicas / 2 + 1;
}

static duct_message_t *duct_check_message(duct_t *duct, uint8_t sender_id, uint8_t receiver_id, duct_flow_index idx) {
    duct_flow_index status = atomic_load(duct->flow_status[sender_id * duct->receiver_replicas + receiver_id]);
    if (status == DUCT_MISSING_FLOW || idx >= status || status > duct->max_flow) {
        return NULL;
    }
    duct_message_t *candidate = duct_lookup_message(duct, sender_id, idx);
    assert(candidate != NULL);
    if (candidate->size == 0 || candidate->size > duct->message_size) {
        return NULL;
    }
    return candidate;
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

    // note: this code is written assuming that the receiver is running in a clip.
    clip_assert();

    uint8_t majority = majority_threshold(txn->duct);
    duct_flow_index best_votes = 0, valid_messages = 0;
    for (uint8_t candidate_id = 0; candidate_id < txn->duct->sender_replicas; candidate_id++) {
        duct_message_t *candidate = duct_check_message(txn->duct, candidate_id, txn->replica_id, txn->flow_current);
        if (candidate == NULL) {
            // invalid data; skip this one.
            continue;
        }
        valid_messages++;
        duct_flow_index votes = 1;
        for (uint8_t compare_id = candidate_id + 1; compare_id < txn->duct->sender_replicas; compare_id++) {
            duct_message_t *compare = duct_check_message(txn->duct, compare_id, txn->replica_id, txn->flow_current);
            if (compare == NULL) {
                // invalid data; skip this one.
                continue;
            }
            if (compare->size != candidate->size || compare->timestamp != candidate->timestamp) {
                // metadata mismatch
                continue;
            }
            if (memcmp(candidate->body, compare->body, compare->size) != 0) {
                // data mismatch
                continue;
            }
            // data and metadata match!
            votes++;
        }
        if (votes >= best_votes) {
            best_votes = votes;
        }
        if (votes >= majority) {
            if (votes != txn->duct->sender_replicas) {
                miscomparef("Voted for a message with %u/%u votes on index %u.",
                            votes, txn->duct->sender_replicas, txn->flow_current);
            }
            if (message_out != NULL) {
                memcpy(message_out, candidate->body, candidate->size);
            }
            if (timestamp_out) {
                *timestamp_out = candidate->timestamp;
            }
            txn->flow_current += 1;
            return candidate->size;
        }
    }

    if (valid_messages != 0) {
        miscomparef("Could not agree on a message: best vote was %u/%u at index %u.",
                    best_votes, txn->duct->sender_replicas, txn->flow_current);
    }

    /* indicate that there are no more valid messages for us to receive */
    return 0;
}

// asserts if we left any messages unprocessed
void duct_receive_commit(duct_txn_t *txn) {
    assert(txn != NULL && txn->duct != NULL);
    assert(txn->mode == DUCT_TXN_RECV);
    assert(txn->replica_id < txn->duct->receiver_replicas);
    assert(txn->flow_current <= txn->duct->max_flow);

    if (duct_receive_message(txn, NULL, NULL) > 0) {
        malfunctionf("Did not consume all messages: consumed %u from space of %u, but there was at least one more.",
                     txn->flow_current, txn->duct->max_flow);
    }

    /* ensure that counts match actual number processed, and mark everything as consumed. */
    for (uint8_t sender_id = 0; sender_id < txn->duct->sender_replicas; sender_id++) {
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
