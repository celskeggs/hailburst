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
    if (pipe_send_desired(s->pipe, s->replica_id)) {
        pipe_send_data(s->pipe, s->replica_id, s->scratch, s->scratch_offset);
        s->scratch_offset = 0;
    }
}

void pipe_receiver_reset(pipe_receiver_t *r) {
    assert(r != NULL);
    r->scratch_offset = r->scratch_avail = 0;
}

void pipe_receiver_prepare(pipe_receiver_t *r) {
    assert(r != NULL);
    assert(r->scratch_offset <= r->scratch_avail);
    size_t refill_level = r->scratch_avail - r->scratch_offset;
    bool request_data = refill_level + pipe_max_rate(r->pipe) <= r->scratch_capacity;
    if (!request_data) {
        pipe_receive_discard(r->pipe, r->replica_id);
        return;
    }
    // relocate existing data if necessary to receive new data
    if (r->scratch_offset > 0 && r->scratch_avail + pipe_max_rate(r->pipe) > r->scratch_capacity) {
        memmove(r->scratch, r->scratch + r->scratch_offset, r->scratch_avail - r->scratch_offset);
        r->scratch_avail -= r->scratch_offset;
        r->scratch_offset = 0;
    }
    // receive new data to end of buffer
    assertf(r->scratch_avail + pipe_max_rate(r->pipe) <= r->scratch_capacity,
            "avail=%zu, offset=%zu, max_rate=%zu, capacity=%zu",
            r->scratch_avail, r->scratch_offset, pipe_max_rate(r->pipe), r->scratch_capacity);
    r->scratch_avail += pipe_receive_data(r->pipe, r->replica_id, r->scratch + r->scratch_avail);
    assert(r->scratch_avail <= r->scratch_capacity);
}

void pipe_receiver_commit(pipe_receiver_t *r) {
    assert(r != NULL);
    assert(r->scratch_capacity >= pipe_max_rate(r->pipe));
    assert(r->scratch_offset <= r->scratch_avail && r->scratch_avail <= r->scratch_capacity);
    size_t refill_level = r->scratch_avail - r->scratch_offset;
    bool request_data = refill_level + pipe_max_rate(r->pipe) <= r->scratch_capacity;
    pipe_receive_indicate(r->pipe, r->replica_id, request_data);
}
