#include <hal/debug.h>
#include <synch/pipe.h>

void pipe_send_prepare(pipe_txn_t *txn, pipe_t *pipe, uint8_t sender_id) {
    assert(txn != NULL && pipe != NULL);
    duct_txn_t ptxn;

    duct_receive_prepare(&ptxn, pipe->pressure, sender_id);
    pipe_status_t status;
    assert(duct_message_size(pipe->pressure) == sizeof(status));
    if (!duct_receive_message(&ptxn, &status, NULL)) {
        status.allowed_flow = 0;
    }
    duct_receive_commit(&ptxn);
    assert(status.allowed_flow <= pipe_max_flow(pipe));

    duct_send_prepare(&txn->data_txn, pipe->dataflow, sender_id);
    txn->pipe = pipe;
    txn->available = status.allowed_flow;
}

bool pipe_send_allowed(pipe_txn_t *txn) {
    assert(txn != NULL);
    return txn->available > 0;
}

void pipe_send_message(pipe_txn_t *txn, void *message, size_t size, local_time_t timestamp) {
    assert(txn != NULL && message != NULL && size >= 1);
    assert(txn->available > 0);
    duct_send_message(&txn->data_txn, message, size, timestamp);
    txn->available -= 1;
}

void pipe_send_commit(pipe_txn_t *txn) {
    assert(txn != NULL);
    duct_send_commit(&txn->data_txn);
}

void pipe_receive_prepare(pipe_txn_t *txn, pipe_t *pipe, uint8_t receiver_id) {
    assert(txn != NULL && pipe != NULL);
    // fetch how much data we requested last time
    txn->pipe = pipe;
    txn->available = pipe->last_requested[receiver_id];
    assert(txn->available <= pipe_max_flow(pipe));
    duct_receive_prepare(&txn->data_txn, pipe->dataflow, receiver_id);
}

size_t pipe_receive_message(pipe_txn_t *txn, void *message_out, local_time_t *timestamp_out) {
    assert(txn != NULL);
    size_t count = duct_receive_message(&txn->data_txn, message_out, timestamp_out);
    if (count > 0 && txn->available > 0) {
        txn->available -= 1;
    }
    return count;
}

void pipe_receive_commit(pipe_txn_t *txn, duct_flow_index requested_count) {
    assert(txn != NULL && txn->pipe != NULL);
    duct_flow_index extra_messages = 0;
    while (duct_receive_message(&txn->data_txn, NULL, NULL) > 0) {
        extra_messages++;
    }
    if (extra_messages > 0) {
        if (txn->available > 0) {
            abortf("pipe %s[receiver=%u]: %u unprocessed requested messages.",
                   txn->pipe->label, duct_txn_replica_id(&txn->data_txn), extra_messages);
        } else {
            debugf(WARNING, "pipe %s[receiver=%u]: received %u additional messages at unexpected time.",
                   txn->pipe->label, duct_txn_replica_id(&txn->data_txn), extra_messages);
        }
    }
    duct_receive_commit(&txn->data_txn);

    assert(requested_count <= pipe_max_flow(txn->pipe));
    pipe_status_t status = {
        .allowed_flow = requested_count,
    };
    assert(duct_message_size(txn->pipe->pressure) == sizeof(status));

    txn->pipe->last_requested[duct_txn_replica_id(&txn->data_txn)] = requested_count;

    duct_txn_t ptxn;
    duct_send_prepare(&ptxn, txn->pipe->pressure, duct_txn_replica_id(&txn->data_txn));
    duct_send_message(&ptxn, &status, sizeof(status), 0);
    duct_send_commit(&ptxn);
}
