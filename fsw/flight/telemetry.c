#include <endian.h>
#include <inttypes.h>

#include <hal/atomic.h>
#include <hal/debug.h>
#include <hal/thread.h>
#include <hal/watchdog.h>
#include <flight/clock.h>
#include <flight/telemetry.h>

enum {
    CMD_RECEIVED_TID          = 0x01000001,
    CMD_COMPLETED_TID         = 0x01000002,
    CMD_NOT_RECOGNIZED_TID    = 0x01000003,
    TLM_DROPPED_TID           = 0x01000004,
    PONG_TID                  = 0x01000005,
    CLOCK_CALIBRATED_TID      = 0x01000006,
    HEARTBEAT_TID             = 0x01000007,
    MAG_PWR_STATE_CHANGED_TID = 0x02000001,
    MAG_READINGS_ARRAY_TID    = 0x02000002,
};

void telemetry_prepare(tlm_txn_t *txn, tlm_endpoint_t *ep, uint8_t sender_id) {
    assert(txn != NULL && ep != NULL);
    txn->ep = ep;
    txn->replica_id = sender_id;
    if (ep->is_synchronous) {
        pipe_send_prepare(&txn->sync_txn, ep->sync_pipe, sender_id);
    } else {
        duct_send_prepare(&txn->async_txn, ep->async_duct, sender_id);
    }
}

bool telemetry_can_send(tlm_txn_t *txn) {
    assert(txn != NULL && txn->ep != NULL);
    if (txn->ep->is_synchronous) {
        return pipe_send_allowed(&txn->sync_txn);
    } else {
        return duct_send_allowed(&txn->async_txn);
    }
}

void telemetry_commit(tlm_txn_t *txn) {
    assert(txn != NULL && txn->ep != NULL);
    if (txn->ep->is_synchronous) {
        pipe_send_commit(&txn->sync_txn);
    } else {
        duct_send_commit(&txn->async_txn);
    }
}

static void telemetry_small_submit(tlm_txn_t *txn, uint32_t telemetry_id, void *data_bytes, size_t data_len) {
    assert(txn != NULL && txn->ep != NULL);
    assert(data_len <= TLM_MAX_ASYNC_SIZE);
    tlm_async_t message = {
        .telemetry_id = telemetry_id,
    };
    if (data_len > 0) {
        assert(data_bytes != NULL);
        memcpy(message.data_bytes, data_bytes, data_len);
    }
    if (txn->ep->is_synchronous) {
        pipe_send_message(&txn->sync_txn, &message, offsetof(tlm_async_t, data_bytes) + data_len, timer_epoch_ns());
    } else {
        duct_send_message(&txn->async_txn, &message, offsetof(tlm_async_t, data_bytes) + data_len, timer_epoch_ns());
    }
}

static uint8_t *telemetry_large_start(tlm_txn_t *txn, uint32_t telemetry_id) {
    assert(txn != NULL && txn->ep != NULL);
    assert(telemetry_can_send(txn) && txn->ep->is_synchronous);
    tlm_sync_t *scratch = &txn->ep->sender_scratch[txn->replica_id];
    scratch->telemetry_id = telemetry_id;
    return scratch->data_bytes;
}

static void telemetry_large_submit(tlm_txn_t *txn, size_t data_len) {
    assert(txn != NULL);
    assert(data_len <= TLM_MAX_SYNC_SIZE && txn->ep->is_synchronous);
    tlm_sync_t *scratch = &txn->ep->sender_scratch[txn->replica_id];
    pipe_send_message(&txn->sync_txn, scratch, offsetof(tlm_sync_t, data_bytes) + data_len, timer_epoch_ns());
}

void telemetry_pump(tlm_replica_t *ts) {
    assert(ts != NULL && ts->mut != NULL && ts->registrations != NULL && ts->replica_id < TELEMETRY_REPLICAS);

    if (clip_is_restart()) {
        // reset encoder
        comm_enc_reset(ts->comm_encoder);
        // clear out all buffered synchronous telemetry
        for (size_t i = 0; i < ts->num_registrations; i++) {
            tlm_registration_replica_t *r = &ts->registrations[i]->replicas[ts->replica_id];

            if (r->is_synchronous) {
                circ_buf_reset(r->receiver_scratch);
            }
        }
    }

    comm_enc_prepare(ts->comm_encoder);

    // stage 1: downlink any reports of dropped telemetry
    if (ts->mut->async_dropped > 0) {
        // if we've been losing data from our ring buffer, report that!

        // convert to big-endian
        uint32_t drop_count = htobe32(ts->mut->async_dropped);

        // fill in telemetry packet
        comm_packet_t packet = {
            .cmd_tlm_id = TLM_DROPPED_TID,
            .timestamp_ns = clock_mission_adjust(timer_epoch_ns()),
            .data_len = sizeof(drop_count),
            .data_bytes = (uint8_t*) &drop_count,
        };

        // transmit this packet
        if (comm_enc_encode(ts->comm_encoder, &packet)) {
            debugf(CRITICAL, "[%u] Telemetry dropped: MessagesLost=%u", ts->replica_id, drop_count);
            // if successful, mark that we downlinked this information.
            ts->mut->async_dropped = 0;
        }
    }

    bool watchdog_ok = false;

    // stage 2: transmit any asynchronous telemetry, and record how many we have to drop
    for (size_t i = 0; i < ts->num_registrations; i++) {
        tlm_registration_replica_t *r = &ts->registrations[i]->replicas[ts->replica_id];

        // handle synchronous telemetry later
        if (r->is_synchronous) {
            continue;
        }

        tlm_async_t message;
        duct_txn_t txn;
        duct_receive_prepare(&txn, r->async_duct, ts->replica_id);
        size_t length = 0;
        local_time_t timestamp = 0;
        while ((length = duct_receive_message(&txn, &message, &timestamp)) > 0) {
            // fill in telemetry packet
            comm_packet_t packet = {
                .cmd_tlm_id = message.telemetry_id,
                .timestamp_ns = clock_mission_adjust(timestamp),
                .data_len = length - offsetof(tlm_async_t, data_bytes),
                .data_bytes = (uint8_t*) message.data_bytes,
            };
            assert(packet.data_len <= TLM_MAX_ASYNC_SIZE);

            debugf(TRACE, "[%u] Transmitting async telemetry, timestamp=" TIMEFMT,
                   ts->replica_id, TIMEARG(packet.timestamp_ns));

            // transmit this packet
            if (comm_enc_encode(ts->comm_encoder, &packet)) {
                watchdog_ok = true;

                debugf(TRACE, "[%u] Transmitted async telemetry.", ts->replica_id);
            } else {
                debugf(WARNING, "[%u] Failed to transmit async telemetry due to full buffer.", ts->replica_id);
                ts->mut->async_dropped++;
            }
        }
        duct_receive_commit(&txn);
    }

    watchdog_indicate(ts->aspect, ts->replica_id, watchdog_ok);

    // stage 3: transmit any synchronous telemetry if we can
    for (size_t i = 0; i < ts->num_registrations; i++) {
        tlm_registration_replica_t *r = &ts->registrations[i]->replicas[ts->replica_id];

        // already handled asynchronous telemetry
        if (!r->is_synchronous) {
            continue;
        }

        // first: pull telemetry from endpoint into the circular buffer
        pipe_txn_t txn;
        pipe_receive_prepare(&txn, r->sync_pipe, ts->replica_id);

        circ_buf_t *circ = r->receiver_scratch;
        tlm_sync_slot_t *slot;
        assert(sizeof(*slot) == circ_buf_elem_size(circ));
        while (
            (slot = circ_buf_write_peek(circ, 0)) != NULL
            && (slot->data_length = pipe_receive_message(&txn, &slot->sync_data, &slot->timestamp)) > 0
        ) {
            circ_buf_write_done(circ, 1);
        }

        // second: attempt to transmit as much telemetry as we can
        while ((slot = circ_buf_read_peek(circ, 0)) != NULL) {
            // fill in telemetry packet
            comm_packet_t packet = {
                .cmd_tlm_id = slot->sync_data.telemetry_id,
                .timestamp_ns = clock_mission_adjust(slot->timestamp),
                .data_len = slot->data_length - offsetof(tlm_sync_t, data_bytes),
                .data_bytes = (uint8_t*) slot->sync_data.data_bytes,
            };
            // TODO: better handling of the cases where the pipe or duct receive length is less than header length
            assert(packet.data_len <= TLM_MAX_SYNC_SIZE);

            debugf(TRACE, "[%u] Transmitting synchronous telemetry, timestamp=" TIMEFMT,
                   ts->replica_id, TIMEARG(packet.timestamp_ns));

            // transmit this packet
            if (!comm_enc_encode(ts->comm_encoder, &packet)) {
                debugf(WARNING, "[%u] Failed to transmit synchronous telemetry due to full buffer... will try again.",
                       ts->replica_id);
                break;
            }

            debugf(TRACE, "[%u] Transmitted synchronous telemetry.", ts->replica_id);
            circ_buf_read_done(circ, 1);
        }

        // third: tell the endpoint how much data we're ready to receive
        pipe_receive_commit(&txn, circ_buf_write_avail(circ));
    }

    comm_enc_commit(ts->comm_encoder);
}

void tlm_cmd_received(tlm_txn_t *txn, uint64_t original_timestamp, uint32_t original_command_id) {
    debugf(DEBUG, "[%u] Command Received: OriginalTimestamp=%"PRIu64" OriginalCommandId=%08x",
           txn->replica_id, original_timestamp, original_command_id);

    struct {
        uint64_t original_timestamp;
        uint32_t original_command_id;
    } __attribute__((packed)) data = {
        .original_timestamp  = htobe64(original_timestamp),
        .original_command_id = htobe32(original_command_id),
    };
    telemetry_small_submit(txn, CMD_RECEIVED_TID, &data, sizeof(data));
}

void tlm_cmd_completed(tlm_txn_t *txn, uint64_t original_timestamp, uint32_t original_command_id, bool success) {
    debugf(DEBUG, "[%u] Command Completed: OriginalTimestamp=%"PRIu64" OriginalCommandId=%08x Success=%d",
           txn->replica_id, original_timestamp, original_command_id, success);

    struct {
        uint64_t original_timestamp;
        uint32_t original_command_id;
        uint8_t  success;
    } __attribute__((packed)) data = {
        .original_timestamp  = htobe64(original_timestamp),
        .original_command_id = htobe32(original_command_id),
        .success             = (success ? 1 : 0),
    };
    telemetry_small_submit(txn, CMD_COMPLETED_TID, &data, sizeof(data));
}

void tlm_cmd_not_recognized(tlm_txn_t *txn, uint64_t original_timestamp, uint32_t original_command_id,
                            uint32_t length) {
    debugf(CRITICAL, "[%u] Command Not Recognized: OriginalTimestamp=%"PRIu64" OriginalCommandId=%08x Length=%u",
           txn->replica_id, original_timestamp, original_command_id, length);

    struct {
        uint64_t original_timestamp;
        uint32_t original_command_id;
        uint32_t length;
    } __attribute__((packed)) data = {
        .original_timestamp  = htobe64(original_timestamp),
        .original_command_id = htobe32(original_command_id),
        .length              = htobe32(length),
    };
    telemetry_small_submit(txn, CMD_NOT_RECOGNIZED_TID, &data, sizeof(data));
}

void tlm_pong(tlm_txn_t *txn, uint32_t ping_id) {
    debugf(INFO, "[%u] Pong: PingId=%08x", txn->replica_id, ping_id);

    struct {
        uint32_t ping_id;
    } __attribute__((packed)) data = {
        .ping_id = htobe32(ping_id),
    };
    telemetry_small_submit(txn, PONG_TID, &data, sizeof(data));
}

void tlm_clock_calibrated(tlm_txn_t *txn, int64_t adjustment) {
    debugf(INFO, "[%u] ClockCalibrated: Adjustment=%"PRId64"", txn->replica_id, adjustment);

    struct {
        int64_t adjustment;
    } __attribute__((packed)) data = {
        .adjustment = htobe64(adjustment),
    };
    telemetry_small_submit(txn, CLOCK_CALIBRATED_TID, &data, sizeof(data));
}

void tlm_heartbeat(tlm_txn_t *txn) {
    debugf(DEBUG, "[%u] Heartbeat", txn->replica_id);

    telemetry_small_submit(txn, HEARTBEAT_TID, NULL, 0);
}

void tlm_mag_pwr_state_changed(tlm_txn_t *txn, bool power_state) {
    debugf(INFO, "[%u] Magnetometer Power State Changed: PowerState=%d", txn->replica_id, power_state);

    struct {
        uint8_t power_state;
    } __attribute__((packed)) data = {
        .power_state = (power_state ? 1 : 0),
    };
    telemetry_small_submit(txn, MAG_PWR_STATE_CHANGED_TID, &data, sizeof(data));
}

void tlm_mag_readings_map(tlm_txn_t *txn, size_t *fetch_count,
                          void (*fetch)(void *param, size_t index, tlm_mag_reading_t *out), void *param) {
    assert(txn != NULL && fetch_count != NULL && fetch != NULL);

    // get the buffer
    uint8_t *data_bytes = telemetry_large_start(txn, MAG_READINGS_ARRAY_TID);

    // now fill up the buffer
    size_t num_readings = *fetch_count;
    if (num_readings * 14 > TLM_MAX_SYNC_SIZE) {
        num_readings = TLM_MAX_SYNC_SIZE / 14;
    }
    assert(num_readings > 0);
    debugf(DEBUG, "[%u] Magnetometer Readings Array: %zu readings", txn->replica_id, num_readings);
    *fetch_count = num_readings;
    uint16_t *out = (uint16_t*) data_bytes;
    for (size_t i = 0; i < num_readings; i++) {
        tlm_mag_reading_t rd;

        fetch(param, i, &rd);

        debugf(DEBUG, "    Readings[%zu]={%"PRIu64", %d, %d, %d}",
               i, rd.reading_time, rd.mag_x, rd.mag_y, rd.mag_z);

        *out++ = htobe16((uint16_t) (rd.reading_time >> 48));
        *out++ = htobe16((uint16_t) (rd.reading_time >> 32));
        *out++ = htobe16((uint16_t) (rd.reading_time >> 16));
        *out++ = htobe16((uint16_t) (rd.reading_time >> 0));

        *out++ = htobe16(rd.mag_x);
        *out++ = htobe16(rd.mag_y);
        *out++ = htobe16(rd.mag_z);
    }
    assert((uint8_t*) out - data_bytes == (ssize_t) (num_readings * 14));

    // write the sync record to the ring buffer, and wait for it to be written out to the telemetry stream
    telemetry_large_submit(txn, (uint8_t*) out - data_bytes);
}
