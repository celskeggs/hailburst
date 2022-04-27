#include <endian.h>
#include <string.h>

#include <hal/atomic.h>
#include <hal/clip.h>
#include <hal/thread.h>
#include <hal/timer.h>
#include <synch/duct.h>
#include <synch/strict.h>

//#define DUCT_DEBUG

void duct_send_prepare(duct_txn_t *txn, duct_t *duct, uint8_t sender_id) {
    assert(txn != NULL && duct != NULL);
    assertf(sender_id < duct->sender_replicas,
            "duct %s[sender=%u]: invalid sender id for %u replicas", duct->label, sender_id, duct->sender_replicas);

#ifdef DUCT_DEBUG
    debugf(TRACE, "duct %s[sender=%u]: prepare send", duct->label, sender_id);
#endif

    txn->mode = DUCT_TXN_SEND;
    txn->duct = duct;
    txn->replica_id = sender_id;
    txn->flow_current = 0;

    /* ensure that all previously-sent flows have been consumed */
    for (uint8_t receiver_id = 0; receiver_id < duct->receiver_replicas; receiver_id++) {
        uint32_t offset = sender_id * duct->receiver_replicas + receiver_id;
        if (atomic_load(duct->flow_status[offset]) != DUCT_MISSING_FLOW) {
            flag_raisef(&duct->flags_send[offset],
                        "duct %s[sender=%u]: receiver has deviated from schedule.",
                        duct->label, sender_id);
        } else {
            flag_recoverf(&duct->flags_send[offset],
                          "duct %s[sender=%u]: receiver has resumed acting on schedule.",
                          duct->label, sender_id);
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
void duct_send_message(duct_txn_t *txn, const void *message, size_t size, local_time_t timestamp) {
    assert(txn != NULL && txn->duct != NULL);
    assert(txn->mode == DUCT_TXN_SEND);
    assert(txn->replica_id < txn->duct->sender_replicas);
    assert(txn->flow_current < txn->duct->max_flow);
    assert(message != NULL);
    assertf(size >= 1 && size <= txn->duct->message_size,
            "invalid message size; %zu not in [1, %zu].", size, txn->duct->message_size);

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
    assertf(txn->mode == DUCT_TXN_SEND,
            "duct %s[sender=%u]: transaction mode was %u instead of DUCT_TXN_SEND (%u)",
            txn->duct->label, txn->replica_id, txn->mode, DUCT_TXN_SEND);
    assert(txn->replica_id < txn->duct->sender_replicas);
    assert(txn->flow_current <= txn->duct->max_flow);

    duct_flow_index flow = txn->flow_current;

    /* emit new counts */
    for (uint8_t receiver_id = 0; receiver_id < txn->duct->receiver_replicas; receiver_id++) {
        atomic_store(txn->duct->flow_status[txn->replica_id * txn->duct->receiver_replicas + receiver_id], flow);
    }

    /* clear transaction */
    txn->mode = DUCT_TXN_INVALID;
    txn->flow_current = DUCT_MISSING_FLOW;

#ifdef DUCT_DEBUG
    debugf(TRACE, "duct %s[sender=%u]: commit send (%u/%u)",
           txn->duct->label, txn->replica_id, flow, txn->duct->max_flow);
#endif
}

void duct_receive_prepare(duct_txn_t *txn, duct_t *duct, uint8_t receiver_id) {
    assert(txn != NULL && duct != NULL);
    assertf(receiver_id < duct->receiver_replicas,
            "duct %s: invalid receiver ID %u >= %u replicas", duct->label, receiver_id, duct->receiver_replicas);

    // ensure that all receives run inside clips, so that if they get descheduled, they don't keep trying to interpret
    // received data that might be actively changing.
    clip_assert();

#ifdef DUCT_DEBUG
    debugf(TRACE, "duct %s[receiver=%u]: prepare receive", duct->label, receiver_id);
#endif

    txn->mode = DUCT_TXN_RECV;
    txn->duct = duct;
    txn->replica_id = receiver_id;
    txn->flow_current = 0;

    /* ensure that all senders have transmitted flows for us */
    for (uint8_t sender_id = 0; sender_id < duct->sender_replicas; sender_id++) {
        uint32_t offset = sender_id * duct->receiver_replicas + receiver_id;
        duct_flow_index status = atomic_load(duct->flow_status[offset]);
        if (status == DUCT_MISSING_FLOW || status > duct->max_flow) {
            atomic_store_relaxed(duct->flow_status[offset], 0);
            flag_raisef(&duct->flags_receive[offset],
                        "duct %s[receiver=%u]: sender %u has deviated from schedule.",
                        duct->label, receiver_id, sender_id);
        } else {
            flag_recoverf(&duct->flags_receive[offset],
                          "duct %s[receiver=%u]: sender %u has resumed acting on schedule.",
                          duct->label, receiver_id, sender_id);
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
size_t duct_receive_message(duct_txn_t *txn, void *message_out, local_time_t *timestamp_out) {
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
#ifdef DUCT_DEBUG
            debugf(TRACE, "duct %s[receiver=%u]: candidate %u: missing or invalid message; skipping.",
                   txn->duct->label, txn->replica_id, candidate_id);
#endif
            continue;
        }
        valid_messages++;
        duct_flow_index votes = 1;
        duct_flow_index total_valid_messages = valid_messages;
        for (uint8_t compare_id = candidate_id + 1; compare_id < txn->duct->sender_replicas; compare_id++) {
            duct_message_t *compare = duct_check_message(txn->duct, compare_id, txn->replica_id, txn->flow_current);
            if (compare == NULL) {
#ifdef DUCT_DEBUG
                debugf(TRACE, "duct %s[receiver=%u]: candidate %u -> compare %u: missing or invalid message; skipping.",
                       txn->duct->label, txn->replica_id, candidate_id, compare_id);
#endif
                continue;
            }
            total_valid_messages++;
            if (compare->size != candidate->size || compare->timestamp != candidate->timestamp) {
                debugf(TRACE, "duct %s[receiver=%u]: candidate %u -> compare %u: "
                       "metadata mismatch (size %zu ? %zu, timestamp " TIMEFMT " ? " TIMEFMT "); skipping.",
                       txn->duct->label, txn->replica_id, candidate_id, compare_id,
                       candidate->size, compare->size, TIMEARG(candidate->timestamp), TIMEARG(compare->timestamp));
                continue;
            }
            if (memcmp(candidate->body, compare->body, compare->size) != 0) {
                debugf(TRACE, "duct %s[receiver=%u]: candidate %u -> compare %u: data mismatch (len %zu); skipping.",
                       txn->duct->label, txn->replica_id, candidate_id, compare_id, compare->size);
                size_t i = 0;
                while (i + 16 <= compare->size) {
                    uint32_t *ca_data = (uint32_t *) &candidate->body[i];
                    uint32_t *co_data = (uint32_t *) &compare->body[i];
                    debugf(TRACE, "[+%3zu] %u. %08x %08x %08x %08x VS %u. %08x %08x %08x %08x",
                           i, candidate_id, htobe32(ca_data[0]), htobe32(ca_data[1]), htobe32(ca_data[2]), htobe32(ca_data[3]),
                              compare_id,   htobe32(co_data[0]), htobe32(co_data[1]), htobe32(co_data[2]), htobe32(co_data[3]));
                    i += 16;
                }
                while (i + 4 <= compare->size) {
                    uint32_t *ca_data = (uint32_t *) &candidate->body[i];
                    uint32_t *co_data = (uint32_t *) &compare->body[i];
                    debugf(TRACE, "[+%3zu] %u. %08x VS %u. %08x",
                           i, candidate_id, htobe32(ca_data[0]),
                              compare_id,   htobe32(co_data[0]));
                    i += 4;
                }
                while (i < compare->size) {
                    debugf(TRACE, "[+%3zu] %u. %02x VS %u. %02x",
                           i, candidate_id, candidate->body[i],
                              compare_id,   compare->body[i]);
                    i += 1;
                }
                continue;
            }
            votes++;
#ifdef DUCT_DEBUG
            debugf(TRACE, "duct %s[receiver=%u]: candidate %u -> compare %u: data match; voting.",
                   txn->duct->label, txn->replica_id, candidate_id, compare_id);
#endif
        }
        if (votes >= best_votes) {
            best_votes = votes;
        }
        if (votes >= majority) {
            if (votes != txn->duct->sender_replicas) {
                miscomparef("duct %s[receiver=%u]: voted for a message with %u/%u/%u votes on index %u.",
                            txn->duct->label, txn->replica_id, votes, total_valid_messages, txn->duct->sender_replicas,
                            txn->flow_current);
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
        miscomparef("duct %s[receiver=%u]: could not agree on a message: best vote was %u/%u/%u at index %u.",
                    txn->duct->label, txn->replica_id,
                    best_votes, valid_messages, txn->duct->sender_replicas, txn->flow_current);
    }

    /* indicate that there are no more valid messages for us to receive */
    return 0;
}

// asserts if we left any messages unprocessed
void duct_receive_commit(duct_txn_t *txn) {
    assert(txn != NULL && txn->duct != NULL);
    assertf(txn->mode == DUCT_TXN_RECV,
            "duct %s[receiver=%u]: transaction mode was %u instead of DUCT_TXN_RECV (%u)",
            txn->duct->label, txn->replica_id, txn->mode, DUCT_TXN_RECV);
    assert(txn->replica_id < txn->duct->receiver_replicas);
    assert(txn->flow_current <= txn->duct->max_flow);

    if (duct_receive_message(txn, NULL, NULL) > 0) {
        malfunctionf("duct %s[receiver=%u]: did not consume all messages: consumed %u from space of %u, but there was "
                     "at least one more.", txn->duct->label, txn->replica_id, txn->flow_current, txn->duct->max_flow);
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
    debugf(TRACE, "duct %s[receiver=%u]: commit receive", txn->duct->label, txn->replica_id);
#endif
}
