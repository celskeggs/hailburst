#include <endian.h>
#include <inttypes.h>

#include <hal/atomic.h>
#include <hal/clock.h>
#include <hal/debug.h>
#include <hal/thread.h>
#include <hal/watchdog.h>
#include <flight/telemetry.h>

struct {
    bool        initialized;
    uint32_t    async_dropped; // atomic
    comm_enc_t *comm_encoder;
} telemetry = {
    .initialized   = false,
    .async_dropped = 0,
};

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

MULTICHART_SERVER_REGISTER(telemetry_async_chart, sizeof(tlm_async_t), ignore_callback, NULL);
MULTICHART_SERVER_REGISTER(telemetry_sync_chart, sizeof(tlm_sync_t), ignore_callback, NULL);

void telemetry_init(comm_enc_t *encoder) {
    assert(!telemetry.initialized);

    telemetry.comm_encoder = encoder;

    telemetry.initialized = true;
}

static tlm_async_t *telemetry_async_start(tlm_async_endpoint_t *tep) {
    assert(tep != NULL);
    tlm_async_t *async = multichart_request_start(tep->client);
    if (async == NULL) {
        // can be relaxed because we don't care about previous writes being retired first
        atomic_fetch_add_relaxed(telemetry.async_dropped, 1);
    }
    return async;
}

static void telemetry_async_send(tlm_async_endpoint_t *tep, tlm_async_t *async) {
    assert(tep != NULL && async != NULL);

    multichart_request_send(tep->client, async);
}

static tlm_sync_t *telemetry_start_sync(tlm_sync_endpoint_t *tep) {
    assert(tep != NULL);
    for (;;) {
        tlm_sync_t *sync = multichart_request_start(tep->sync_client);
        if (sync != NULL) {
            return sync;
        }
        local_doze(tep->client_task);
    }
}

static void telemetry_record_sync(tlm_sync_endpoint_t *tep, tlm_sync_t *sync, size_t data_len) {
    assert(telemetry.initialized);

    sync->data_len = data_len;
    multichart_request_send(tep->sync_client, sync);

    // wait for request to complete
    assert(multichart_client_note_count(tep->sync_client) == 1);
    while (multichart_request_start(tep->sync_client) == NULL) {
        local_doze(tep->client_task);
    }
}

void telemetry_main_clip(void) {
    assert(telemetry.initialized == true);
    assert(telemetry.comm_encoder != NULL);

    if (clip_is_restart()) {
        comm_enc_reset(telemetry.comm_encoder);
    }

    comm_enc_prepare(telemetry.comm_encoder);

    comm_packet_t packet;

    for (;;) {
        if (atomic_load_relaxed(telemetry.async_dropped) > 0) {
            // if we've been losing data from our ring buffer, we should probably report that first!

            // this fetches the lastest drop count and replaces it with zero by atomic binary-and with 0.
            uint32_t drop_count = atomic_load_relaxed(telemetry.async_dropped);

            debugf(CRITICAL, "Telemetry dropped: MessagesLost=%u", drop_count);

            // convert to big-endian
            drop_count = htobe32(drop_count);

            // fill in telemetry packet
            packet.cmd_tlm_id   = TLM_DROPPED_TID;
            packet.timestamp_ns = clock_timestamp();
            packet.data_len     = sizeof(drop_count);
            packet.data_bytes   = (uint8_t*) &drop_count;

            // transmit this packet
            if (comm_enc_encode(telemetry.comm_encoder, &packet)) {
                // if successful, mark that we downlinked this information. (but leave any new drops that just came in.)
                atomic_fetch_sub_relaxed(telemetry.async_dropped, drop_count);
            } else {
                break;
            }

            // fall through here so that we actually transmit at least one real async packet per loop
            // (and that we don't just keep dropping everything)
        }

        // pull telemetry from multichart
        uint64_t timestamp_ns_mono = 0;
        tlm_async_t *async_elm = multichart_reply_start(&telemetry_async_chart, &timestamp_ns_mono);
        if (async_elm != NULL) {
            // convert async element to packet
            packet.cmd_tlm_id   = async_elm->telemetry_id;
            packet.timestamp_ns = clock_adjust_monotonic(timestamp_ns_mono);
            packet.data_len     = async_elm->data_len;
            packet.data_bytes   = async_elm->data_bytes; // array->pointer

            debugf(TRACE, "Transmitting async telemetry, timestamp=%u.%09u",
                       (uint32_t) (packet.timestamp_ns / CLOCK_NS_PER_SEC),
                       (uint32_t) (packet.timestamp_ns % CLOCK_NS_PER_SEC));

            // transmit this packet
            if (comm_enc_encode(telemetry.comm_encoder, &packet)) {
                // if successful,
                multichart_reply_send(&telemetry_async_chart, async_elm);

                watchdog_ok(WATCHDOG_ASPECT_TELEMETRY);

                debugf(TRACE, "Transmitted async telemetry.");
            } else {
                debugf(WARNING, "Failed to transmit async telemetry due to full buffer... will try again.");
                break;
            }
        } else {
            // pull synchronous telemetry from chart (only if all async telemetry is processed)
            tlm_sync_t *sync_elm = multichart_reply_start(&telemetry_sync_chart, &timestamp_ns_mono);
            if (sync_elm != NULL) {
                assert(sync_elm->data_len <= TLM_MAX_SYNC_SIZE);

                // convert sync element to packet
                packet.cmd_tlm_id   = sync_elm->telemetry_id;
                packet.timestamp_ns = clock_adjust_monotonic(timestamp_ns_mono);
                packet.data_len     = sync_elm->data_len;
                packet.data_bytes   = sync_elm->data_bytes;

                debugf(TRACE, "Transmitting synchronous telemetry, timestamp=%u.%09u",
                       (uint32_t) (packet.timestamp_ns / CLOCK_NS_PER_SEC),
                       (uint32_t) (packet.timestamp_ns % CLOCK_NS_PER_SEC));

                // transmit this packet
                if (comm_enc_encode(telemetry.comm_encoder, &packet)) {
                    // let the synchronous sender know they can continue
                    multichart_reply_send(&telemetry_sync_chart, sync_elm);

                    debugf(TRACE, "Transmitted synchronous telemetry.");
                } else {
                    debugf(WARNING, "Failed to transmit synchronous telemetry due to full buffer... will try again.");
                    break;
                }
            } else {
                // no asynchronous telemetry AND no synchronous telemetry... time to sleep!
                break;
            }
        }
    }

    comm_enc_commit(telemetry.comm_encoder);
}

CLIP_REGISTER(telemetry_task, telemetry_main_clip, NULL);

void tlm_cmd_received(tlm_async_endpoint_t *tep, uint64_t original_timestamp, uint32_t original_command_id) {
    debugf(DEBUG, "Command Received: OriginalTimestamp=%"PRIu64" OriginalCommandId=%08x",
           original_timestamp, original_command_id);

    tlm_async_t *tlm = telemetry_async_start(tep);
    if (!tlm) {
        return;
    }
    tlm->telemetry_id = CMD_RECEIVED_TID;
    tlm->data_len = 12;
    uint32_t *out = (uint32_t*) tlm->data_bytes;
    *out++ = htobe32((uint32_t) (original_timestamp >> 32));
    *out++ = htobe32((uint32_t) original_timestamp);
    *out++ = htobe32(original_command_id);
    telemetry_async_send(tep, tlm);
}

void tlm_cmd_completed(tlm_async_endpoint_t *tep, uint64_t original_timestamp, uint32_t original_command_id,
                       bool success) {
    debugf(DEBUG, "Command Completed: OriginalTimestamp=%"PRIu64" OriginalCommandId=%08x Success=%d",
           original_timestamp, original_command_id, success);

    tlm_async_t *tlm = telemetry_async_start(tep);
    if (!tlm) {
        return;
    }
    tlm->telemetry_id = CMD_COMPLETED_TID;
    tlm->data_len = 13;
    uint32_t *out = (uint32_t*) tlm->data_bytes;
    *out++ = htobe32((uint32_t) (original_timestamp >> 32));
    *out++ = htobe32((uint32_t) original_timestamp);
    *out++ = htobe32(original_command_id);
    tlm->data_bytes[12] = (success ? 1 : 0);
    telemetry_async_send(tep, tlm);
}

void tlm_cmd_not_recognized(tlm_async_endpoint_t *tep, uint64_t original_timestamp, uint32_t original_command_id,
                            uint32_t length) {
    debugf(CRITICAL, "Command Not Recognized: OriginalTimestamp=%"PRIu64" OriginalCommandId=%08x Length=%u",
           original_timestamp, original_command_id, length);

    tlm_async_t *tlm = telemetry_async_start(tep);
    if (!tlm) {
        return;
    }
    tlm->telemetry_id = CMD_NOT_RECOGNIZED_TID;
    tlm->data_len = 16;
    uint32_t *out = (uint32_t*) tlm->data_bytes;
    *out++ = htobe32((uint32_t) (original_timestamp >> 32));
    *out++ = htobe32((uint32_t) original_timestamp);
    *out++ = htobe32(original_command_id);
    *out++ = htobe32(length);
    telemetry_async_send(tep, tlm);
}

void tlm_pong(tlm_async_endpoint_t *tep, uint32_t ping_id) {
    debugf(INFO, "Pong: PingId=%08x", ping_id);

    tlm_async_t *tlm = telemetry_async_start(tep);
    if (!tlm) {
        return;
    }
    tlm->telemetry_id = PONG_TID;
    tlm->data_len = 4;
    uint32_t *out = (uint32_t*) tlm->data_bytes;
    *out++ = htobe32(ping_id);
    telemetry_async_send(tep, tlm);
}

void tlm_clock_calibrated(tlm_async_endpoint_t *tep, int64_t adjustment) {
    debugf(INFO, "ClockCalibrated: Adjustment=%"PRId64"", adjustment);

    tlm_async_t *tlm = telemetry_async_start(tep);
    if (!tlm) {
        return;
    }
    tlm->telemetry_id = CLOCK_CALIBRATED_TID;
    tlm->data_len = 8;
    uint32_t *out = (uint32_t*) tlm->data_bytes;
    *out++ = htobe32((uint32_t) (adjustment >> 32));
    *out++ = htobe32((uint32_t) (adjustment >> 0));
    telemetry_async_send(tep, tlm);
}

void tlm_heartbeat(tlm_async_endpoint_t *tep) {
    debugf(DEBUG, "Heartbeat");

    tlm_async_t *tlm = telemetry_async_start(tep);
    if (!tlm) {
        return;
    }
    tlm->telemetry_id = HEARTBEAT_TID;
    tlm->data_len = 0;
    telemetry_async_send(tep, tlm);
}

void tlm_mag_pwr_state_changed(tlm_async_endpoint_t *tep, bool power_state) {
    debugf(INFO, "Magnetometer Power State Changed: PowerState=%d", power_state);

    tlm_async_t *tlm = telemetry_async_start(tep);
    if (!tlm) {
        return;
    }
    tlm->telemetry_id = MAG_PWR_STATE_CHANGED_TID;
    tlm->data_len = 1;
    tlm->data_bytes[0] = (power_state ? 1 : 0);
    telemetry_async_send(tep, tlm);
}

void tlm_sync_mag_readings_map(tlm_sync_endpoint_t *tep, size_t *fetch_count,
                               void (*fetch)(void *param, size_t index, tlm_mag_reading_t *out), void *param) {
    assert(tep != NULL && fetch_count != NULL && fetch != NULL);

    // get the buffer
    tlm_sync_t *sync = telemetry_start_sync(tep);

    // now fill up the buffer
    sync->telemetry_id = MAG_READINGS_ARRAY_TID;
    uint16_t *out = (uint16_t*) sync->data_bytes;
    size_t num_readings = *fetch_count;
    assert(multichart_client_note_size(tep->sync_client) == sizeof(tlm_sync_t));
    static_assert(sizeof(tlm_sync_t) == TLM_MAX_SYNC_SIZE + offsetof(tlm_sync_t, data_bytes), "size validity");
    if (num_readings * 14 > TLM_MAX_SYNC_SIZE) {
        num_readings = TLM_MAX_SYNC_SIZE / 14;
    }
    assert(num_readings > 0);
    debugf(DEBUG, "Magnetometer Readings Array: %zu readings", num_readings);
    *fetch_count = num_readings;
    for (size_t i = 0; i < num_readings; i++) {
        tlm_mag_reading_t rd;

        fetch(param, i, &rd);

        debugf(DEBUG, "  Readings[%zu]={%"PRIu64", %d, %d, %d}",
               i, rd.reading_time, rd.mag_x, rd.mag_y, rd.mag_z);

        *out++ = htobe16((uint16_t) (rd.reading_time >> 48));
        *out++ = htobe16((uint16_t) (rd.reading_time >> 32));
        *out++ = htobe16((uint16_t) (rd.reading_time >> 16));
        *out++ = htobe16((uint16_t) (rd.reading_time >> 0));

        *out++ = htobe16(rd.mag_x);
        *out++ = htobe16(rd.mag_y);
        *out++ = htobe16(rd.mag_z);
    }
    assert((uint8_t*) out - sync->data_bytes == (ssize_t) (num_readings * 14));

    // write the sync record to the ring buffer, and wait for it to be written out to the telemetry stream
    telemetry_record_sync(tep, sync, (uint8_t*) out - sync->data_bytes);
}
