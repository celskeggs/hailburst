#ifndef FSW_SYNCH_PIPE_H
#define FSW_SYNCH_PIPE_H

/*
 * This file contains the interface for a replication-safe pipe built on top of the duct system. A pipe is simply a
 * duct that supports backpressure (implemented by another duct).
 *
 * Buffering is not handled here: see pipebuf.h for buffering support.
 */

#include <synch/duct.h>

enum pipe_polarity {
    PIPE_SENDER_FIRST,
    PIPE_RECEIVER_FIRST,
};

typedef struct {
    // TODO: should there be a version of the pipe where this is measured in bytes, rather than messages?
    duct_flow_index allowed_flow;
} pipe_status_t;

typedef const struct {
    const char *label;
    size_t *last_requested; // array indexed by receiver
    duct_t *dataflow;
    duct_t *pressure;
} pipe_t;

typedef struct {
    pipe_t         *pipe;
    duct_flow_index available;
    duct_txn_t      data_txn;
} pipe_txn_t;

macro_define(PIPE_REGISTER,
             p_ident, p_sender_replicas, p_receiver_replicas, p_max_flow, p_msg_size, p_polarity) {
    DUCT_REGISTER(symbol_join(p_ident, dataflow), p_sender_replicas, p_receiver_replicas, p_max_flow, p_msg_size,
                  (p_polarity == PIPE_SENDER_FIRST) ? DUCT_SENDER_FIRST : DUCT_RECEIVER_FIRST);
    DUCT_REGISTER(symbol_join(p_ident, pressure), p_receiver_replicas, p_sender_replicas, 1, sizeof(pipe_status_t),
                  (p_polarity == PIPE_SENDER_FIRST) ? DUCT_RECEIVER_FIRST : DUCT_SENDER_FIRST);
    size_t symbol_join(p_ident, last_requested)[p_receiver_replicas] = { 0 };
    pipe_t p_ident = {
        .label = symbol_str(p_ident),
        .last_requested = symbol_join(p_ident, last_requested),
        .dataflow = &symbol_join(p_ident, dataflow),
        .pressure = &symbol_join(p_ident, pressure),
    }
}

static inline size_t pipe_message_size(pipe_t *pipe) {
    assert(pipe != NULL);
    return duct_message_size(pipe->dataflow);
}

static inline duct_flow_index pipe_max_flow(pipe_t *pipe) {
    assert(pipe != NULL);
    return duct_max_flow(pipe->dataflow);
}

void pipe_send_prepare(pipe_txn_t *txn, pipe_t *pipe, uint8_t sender_id);
bool pipe_send_allowed(pipe_txn_t *txn);
void pipe_send_message(pipe_txn_t *txn, void *message, size_t size, local_time_t timestamp);
void pipe_send_commit(pipe_txn_t *txn);

void pipe_receive_prepare(pipe_txn_t *txn, pipe_t *pipe, uint8_t receiver_id);
size_t pipe_receive_message(pipe_txn_t *txn, void *message_out, local_time_t *timestamp_out);
void pipe_receive_commit(pipe_txn_t *txn, duct_flow_index requested_count);

#endif /* FSW_SYNCH_PIPE_H */
