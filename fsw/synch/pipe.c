#include <hal/thread.h>
#include <synch/pipe.h>

// service the pipe in an epoch where no transmission is desired. either this or pipe_send_desired must be called each
// epoch.
void pipe_send_idle(pipe_t *pipe, uint8_t sender_id) {
    duct_txn_t txn;
    duct_receive_prepare(&txn, pipe->pressure, sender_id);
    // discard any received data
    (void) duct_receive_message(&txn, NULL, NULL);
    duct_receive_commit(&txn);
    duct_send_prepare(&txn, pipe->dataflow, sender_id);
    duct_send_commit(&txn);
}

// check whether transmission is possible on the pipe. if this returns true, pipe_send_data MUST be called before the
// end of the epoch. if this returns false, pipe_send_data MUST NOT be called.
bool pipe_send_desired(pipe_t *pipe, uint8_t sender_id) {
    duct_txn_t txn;
    duct_receive_prepare(&txn, pipe->pressure, sender_id);
    pipe_status_t status;
    assert(duct_message_size(pipe->pressure) == sizeof(status));
    if (!duct_receive_message(&txn, &status, NULL)) {
        status.allow_flow = false;
    }
    duct_receive_commit(&txn);

    if (!status.allow_flow) {
        duct_send_prepare(&txn, pipe->dataflow, sender_id);
        duct_send_commit(&txn);
    }

    return status.allow_flow;
}

void pipe_send_data(pipe_t *pipe, uint8_t sender_id, void *message, size_t size) {
    duct_txn_t txn;
    duct_send_prepare(&txn, pipe->dataflow, sender_id);
    if (size > 0) {
        assert(message != NULL);
        duct_send_message(&txn, message, size, 0);
    }
    duct_send_commit(&txn);
}

// must be called every epoch. if request_data, then data will be available the next epoch.
void pipe_receive_indicate(pipe_t *pipe, uint8_t receiver_id, bool request_data) {
    duct_txn_t txn;
    duct_send_prepare(&txn, pipe->pressure, receiver_id);
    pipe_status_t status = {
        .allow_flow = request_data,
    };
    assert(duct_message_size(pipe->pressure) == sizeof(status));
    duct_send_message(&txn, &status, sizeof(status), 0);
    duct_send_commit(&txn);
}

// must be called every epoch if data is not expected.
void pipe_receive_discard(pipe_t *pipe, uint8_t receiver_id) {
    size_t message_len = pipe_receive_data(pipe, receiver_id, NULL);
    if (message_len > 0) {
        debugf(WARNING, "Pipe recipient (%s) received stream data (%zu bytes) at unexpected time.",
               task_get_name(task_get_current()), message_len);
    }
}

// must be called every epoch if data is expected. returns nonzero result if data is available.
size_t pipe_receive_data(pipe_t *pipe, uint8_t receiver_id, void *message_out) {
    duct_txn_t txn;
    duct_receive_prepare(&txn, pipe->dataflow, receiver_id);
    size_t size = duct_receive_message(&txn, message_out, NULL);
    duct_receive_commit(&txn);
    return size;
}
