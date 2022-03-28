#ifndef FSW_SYNCH_PIPE_H
#define FSW_SYNCH_PIPE_H

/*
 * This file contains the interface for a replication-safe pipe built on top of the duct system. It works by combining
 * two ducts: one for data to be sent, and one for backpressure to be indicated.
 *
 * Buffering is not handled here: see pipebuf.h for buffering support.
 */

#include <synch/duct.h>

enum pipe_polarity {
    PIPE_SENDER_FIRST,
    PIPE_RECEIVER_FIRST,
};

typedef struct {
    bool allow_flow;
} pipe_status_t;

typedef struct {
    duct_t *dataflow;
    duct_t *pressure;
} pipe_t;

macro_define(PIPE_REGISTER,
             p_ident, p_sender_replicas, p_receiver_replicas, p_max_rate, p_polarity) {
    DUCT_REGISTER(symbol_join(p_ident, dataflow), p_sender_replicas, p_receiver_replicas, 1, p_max_rate,
                  (p_polarity == PIPE_SENDER_FIRST) ? DUCT_SENDER_FIRST : DUCT_RECEIVER_FIRST);
    DUCT_REGISTER(symbol_join(p_ident, pressure), p_receiver_replicas, p_sender_replicas, 1, sizeof(pipe_status_t),
                  (p_polarity == PIPE_SENDER_FIRST) ? DUCT_RECEIVER_FIRST : DUCT_SENDER_FIRST);
    pipe_t p_ident = {
        .dataflow = &symbol_join(p_ident, dataflow),
        .pressure = &symbol_join(p_ident, pressure),
    };
}

static inline size_t pipe_max_rate(pipe_t *pipe) {
    assert(pipe != NULL);
    return duct_message_size(pipe->dataflow);
}

// service the pipe in an epoch where no transmission is desired. either this or pipe_send_desired must be called each
// epoch.
void pipe_send_idle(pipe_t *pipe, uint8_t sender_id);
// check whether transmission is possible on the pipe. if this returns true, pipe_send_data MUST be called before the
// end of the epoch. if this returns false, pipe_send_data MUST NOT be called.
bool pipe_send_desired(pipe_t *pipe, uint8_t sender_id);
void pipe_send_data(pipe_t *pipe, uint8_t sender_id, void *message, size_t size);

// must be called every epoch. if request_data, then data will be available the next epoch.
void pipe_receive_indicate(pipe_t *pipe, uint8_t receiver_id, bool request_data);
// must be called every epoch if data is not expected.
void pipe_receive_discard(pipe_t *pipe, uint8_t receiver_id);
// must be called every epoch if data is expected. returns nonzero result if data is available.
size_t pipe_receive_data(pipe_t *pipe, uint8_t receiver_id, void *message_out);

#endif /* FSW_SYNCH_PIPE_H */
