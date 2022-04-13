#include <synch/pipebuf.h>

void pipe_sender_reset(pipe_sender_t *s) {
    assert(s != NULL);
    s->scratch_offset = 0;
}

void pipe_sender_prepare(pipe_sender_t *s) {
    assert(s != NULL);
    assert(s->scratch_offset <= s->scratch_capacity);
}

void pipe_sender_commit(pipe_sender_t *s) {
    assert(s != NULL);
    assert(s->scratch_offset <= s->scratch_capacity);
    pipe_txn_t txn;
    pipe_send_prepare(&txn, s->pipe, s->replica_id);
    while (s->scratch_offset > 0 && pipe_send_allowed(&txn)) {
        size_t send_len = s->scratch_offset;
        if (send_len > pipe_message_size(s->pipe)) {
            send_len = pipe_message_size(s->pipe);
        }
        pipe_send_message(&txn, s->scratch, send_len, 0);
        if (send_len < s->scratch_offset) {
            s->scratch_offset -= send_len;
            memmove(s->scratch, s->scratch + send_len, s->scratch_offset);
        } else {
            s->scratch_offset = 0;
        }
    }
    pipe_send_commit(&txn);
}

void pipe_receiver_reset(pipe_receiver_t *r) {
    assert(r != NULL);
    r->scratch_offset = r->scratch_avail = 0;
}

static duct_flow_index pipe_receiver_request_count(pipe_receiver_t *r) {
    assert(r != NULL);
    assert(r->scratch_offset <= r->scratch_avail && r->scratch_avail <= r->scratch_capacity);
    // ensure that data can be pipelined, to avoid stalling due to incompletely-consumed data
    assert(2 * pipe_message_size(r->pipe) <= r->scratch_capacity);
    size_t fill_level = r->scratch_avail - r->scratch_offset;
    size_t current_receivable = r->scratch_capacity - fill_level;
    // round down; if we can receive less than a single message, that means we can't receive anything at all!
    size_t refills = current_receivable / pipe_message_size(r->pipe);
    if (refills > pipe_max_flow(r->pipe)) {
        refills = pipe_max_flow(r->pipe);
    }
    return refills;
}

void pipe_receiver_prepare(pipe_receiver_t *r) {
    assert(r != NULL);
    pipe_receive_prepare(&r->pipe_txn, r->pipe, r->replica_id);
    size_t refills = pipe_receiver_request_count(r);
    if (refills == 0) {
        return;
    }
    // relocate existing data if necessary to receive new data
    if (r->scratch_offset > 0 && r->scratch_avail + pipe_message_size(r->pipe) * refills > r->scratch_capacity) {
        memmove(r->scratch, r->scratch + r->scratch_offset, r->scratch_avail - r->scratch_offset);
        r->scratch_avail -= r->scratch_offset;
        r->scratch_offset = 0;
    }
    // receive new data to end of buffer
    while (refills > 0) {
        assertf(r->scratch_avail + pipe_message_size(r->pipe) * refills <= r->scratch_capacity,
                "avail=%zu, offset=%zu, message_size=%zu, refills=%zu, capacity=%zu",
                r->scratch_avail, r->scratch_offset, pipe_message_size(r->pipe), refills, r->scratch_capacity);
        r->scratch_avail += pipe_receive_message(&r->pipe_txn, r->scratch + r->scratch_avail, NULL);
        refills -= 1;
    }
    assert(r->scratch_avail <= r->scratch_capacity);
}

void pipe_receiver_commit(pipe_receiver_t *r) {
    assert(r != NULL);
    assert(r->scratch_capacity >= pipe_message_size(r->pipe));
    size_t refills = pipe_receiver_request_count(r);
    pipe_receive_commit(&r->pipe_txn, refills);
}
