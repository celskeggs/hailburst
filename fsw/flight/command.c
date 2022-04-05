#include <endian.h>
#include <inttypes.h>
#include <stdbool.h>

#include <hal/debug.h>
#include <synch/config.h>
#include <flight/command.h>
#include <flight/telemetry.h>

void command_execution_clip(cmd_replica_t *cr) {
    assert(cr != NULL && cr->system != NULL && cr->system->endpoints != NULL && cr->decoder != NULL);

    if (clip_is_restart()) {
        comm_dec_reset(cr->decoder);
    }

    comm_dec_prepare(cr->decoder);
    tlm_txn_t telem;
    telemetry_prepare(&telem, cr->system->telemetry, cr->replica_id);

    // only process one command per epoch
    comm_packet_t packet;
    bool has_command = comm_dec_decode(cr->decoder, &packet);

    if (has_command) {
        // confirm reception
        tlm_cmd_received(&telem, packet.timestamp_ns, packet.cmd_tlm_id);
    }

    // search through endpoints for a match, and service the ducts while we're at it.
    bool matched = false;
    for (size_t i = 0; i < cr->system->num_endpoints; i++) {
        cmd_endpoint_t *ce = cr->system->endpoints[i];
        assert(ce != NULL);
        duct_txn_t txn;
        duct_send_prepare(&txn, ce->duct, cr->replica_id);
        if (has_command && packet.cmd_tlm_id == ce->cid && packet.data_len <= COMMAND_MAX_PARAM_LENGTH) {
            struct cmd_duct_msg duct_msg = {
                .timestamp = packet.timestamp_ns,
                /* need to populate data using memcpy */
            };
            assert(packet.data_len <= sizeof(duct_msg.data));
            memcpy(duct_msg.data, packet.data_bytes, packet.data_len);
            duct_send_message(&txn, &duct_msg, sizeof(mission_time_t) + packet.data_len, 0);
            matched = true;
        }
        duct_send_commit(&txn);
    }

    if (has_command && !matched) {
        // if we don't recognize the command ID, report that.
        tlm_cmd_not_recognized(&telem, packet.timestamp_ns, packet.cmd_tlm_id, packet.data_len);
    }

    telemetry_commit(&telem);
    comm_dec_commit(cr->decoder);
}

void *command_receive(cmd_endpoint_t *ce, uint8_t replica_id, size_t *size_out) {
    assert(ce != NULL && size_out != NULL);

    struct cmd_endpoint_mut *mut = &ce->mut_replicas[replica_id];

    duct_txn_t txn;
    duct_receive_prepare(&txn, ce->duct, replica_id);
    assert(duct_message_size(ce->duct) == sizeof(mut->last_received));
    size_t msg_size = duct_receive_message(&txn, &mut->last_received, NULL);
    duct_receive_commit(&txn);
    if (msg_size == 0) {
        // discard message
        return NULL;
    } else if (msg_size < sizeof(mission_time_t)) {
        // also discard message
        miscomparef("endpoint received command from command switch without complete header");
        return NULL;
    } else {
        mut->has_outstanding_reply = true;
        *size_out = mut->last_data_length = msg_size - sizeof(mission_time_t);
        return mut->last_received.data;
    }
}

// to indicate completion (OK, FAIL, or UNRECOGNIZED), call this function. it will transmit one telemetry message.
void command_reply(cmd_endpoint_t *ce, uint8_t replica_id, tlm_txn_t *telem, cmd_status_t status) {
    assert(ce != NULL && telem != NULL);
    struct cmd_endpoint_mut *mut = &ce->mut_replicas[replica_id];

    assert(mut->has_outstanding_reply == true);
    mut->has_outstanding_reply = false;
    if (status == CMD_STATUS_UNRECOGNIZED) {
        tlm_cmd_not_recognized(telem, mut->last_received.timestamp, ce->cid, mut->last_data_length);
    } else if (status == CMD_STATUS_OK || status == CMD_STATUS_FAIL) {
        tlm_cmd_completed(telem, mut->last_received.timestamp, ce->cid, status == CMD_STATUS_OK);
    } else {
        abortf("invalid command status: %u", status);
    }
}
