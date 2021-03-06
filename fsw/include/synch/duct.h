#ifndef FSW_SYNCH_DUCT_H
#define FSW_SYNCH_DUCT_H

/*
 * This file contains an implementation of a "redundant communication duct." A duct is a limited-flow-rate
 * communication primitive used for communication between replicated processes.
 *
 * The key idea is this: a duct communicates in ONE direction. A normal queue, however, actually communicates in two
 * directions, because it has to apply backpressure when the sender wants to write too many elements into the queue.
 * In order to actually only communicate in one direction, a duct must set a maximum flow rate. The sender is limited
 * to the same rate regardless of the behavior of the receiver, and the receiver is mandated to accept the full flow
 * rate worth of messages every scheduling epoch.
 *
 * If the receiver fails to hold up its end of the deal, an assertion is tripped.
 */

#include <stdbool.h>
#include <stdint.h>

#include <hal/debug.h>
#include <synch/flag.h>

enum {
    DUCT_MIN_REPLICAS =   1,
    DUCT_MAX_REPLICAS =   4, /* limit this because we need to define an array of this size in each transaction */
    DUCT_MIN_FLOW     =   1,
    DUCT_MAX_FLOW     = 254, /* don't allow 255 flow per epoch to avoid overflow of uint8_t variables */

    DUCT_MISSING_FLOW = 255,
};

typedef uint8_t duct_flow_index;

typedef const struct {
    const char      *label;
    uint8_t          sender_replicas;
    uint8_t          receiver_replicas;
    duct_flow_index  max_flow;
    size_t           message_size;
    uint8_t         *message_buffer;
    duct_flow_index *flow_status; // DUCT_MISSING_FLOW if not sent; otherwise [0, max_flow] based on msgs.
    flag_t          *flags_receive;
    flag_t          *flags_send;
} duct_t;

enum duct_txn_mode {
    DUCT_TXN_INVALID = 0,
    DUCT_TXN_SEND = 1,
    DUCT_TXN_RECV = 2,
};

typedef struct {
    enum duct_txn_mode mode;
    duct_t            *duct;
    uint8_t            replica_id;
    duct_flow_index    flow_current;
} duct_txn_t;

typedef struct {
    size_t       size;
    /* TODO: consider eliminating this, and requiring users incorporate timestamp information in the body itself */
    local_time_t timestamp;
    uint8_t      body[];
} duct_message_t;

enum duct_polarity {
    DUCT_SENDER_FIRST,
    DUCT_RECEIVER_FIRST,
};

macro_define(DUCT_REGISTER,
             d_ident, d_sender_replicas, d_receiver_replicas, d_max_flow, d_message_size, d_polarity) {
    static_assert(DUCT_MIN_REPLICAS <= (d_sender_replicas) && (d_sender_replicas) <= DUCT_MAX_REPLICAS,
                  "invalid number of replicas for sender");
    static_assert(DUCT_MIN_REPLICAS <= (d_receiver_replicas) && (d_receiver_replicas) <= DUCT_MAX_REPLICAS,
                  "invalid number of replicas for receiver");
    static_assert(DUCT_MIN_FLOW <= (d_max_flow) && (d_max_flow) <= DUCT_MAX_FLOW,
                  "invalid max flow setting for duct");
    static_assert(d_message_size >= 1, "invalid message size setting");
    uint8_t symbol_join(d_ident, buf)[
        (d_sender_replicas) * (d_max_flow) * (sizeof(duct_message_t) + (d_message_size))
    ];
    duct_flow_index symbol_join(d_ident, flow_statuses)[(d_sender_replicas) * (d_receiver_replicas)] = {
        [0 ... ((d_sender_replicas) * (d_receiver_replicas) - 1)] =
                ((d_polarity) == DUCT_SENDER_FIRST) ? DUCT_MISSING_FLOW : 0,
    };
    flag_t symbol_join(d_ident, flags_receive)[(d_sender_replicas) * (d_receiver_replicas)];
    flag_t symbol_join(d_ident, flags_send)[(d_sender_replicas) * (d_receiver_replicas)];
    duct_t d_ident = {
        .label = symbol_str(d_ident),
        .sender_replicas = (d_sender_replicas),
        .receiver_replicas = (d_receiver_replicas),
        .max_flow = (d_max_flow),
        .message_size = (d_message_size),
        .message_buffer = symbol_join(d_ident, buf),
        .flow_status = symbol_join(d_ident, flow_statuses),
        .flags_receive = symbol_join(d_ident, flags_receive),
        .flags_send = symbol_join(d_ident, flags_send),
    }
}

static inline size_t duct_message_size(duct_t *duct) {
    assert(duct != NULL);
    return duct->message_size;
}

static inline duct_flow_index duct_max_flow(duct_t *duct) {
    assert(duct != NULL);
    return duct->max_flow;
}

static inline duct_message_t *duct_lookup_message(duct_t *duct, uint8_t sender_id, duct_flow_index flow_index) {
    assert(duct != NULL);
    assert(sender_id < duct->sender_replicas);
    assert(flow_index < duct->max_flow);
    return (duct_message_t *) &duct->message_buffer[
        ((sender_id * duct->max_flow) + flow_index) * (sizeof(duct_message_t) + duct->message_size)
    ];
}

static inline uint8_t duct_txn_replica_id(duct_txn_t *txn) {
    assert(txn != NULL);
    return txn->replica_id;
}

void duct_send_prepare(duct_txn_t *txn, duct_t *duct, uint8_t sender_id);
// returns true if we're allowed to send at least one more message
bool duct_send_allowed(duct_txn_t *txn);
// asserts if we've used up our max flow in this transaction already
void duct_send_message(duct_txn_t *txn, const void *message_in, size_t size, local_time_t timestamp);
void duct_send_commit(duct_txn_t *txn);

void duct_receive_prepare(duct_txn_t *txn, duct_t *duct, uint8_t receiver_id);
// returns size > 0 if a message was successfully received. if size = 0, then we're done with this transaction.
size_t duct_receive_message(duct_txn_t *txn, void *message_out, local_time_t *timestamp_out);
// asserts if we left any messages unprocessed
void duct_receive_commit(duct_txn_t *txn);

#endif /* FSW_SYNCH_DUCT_H */
