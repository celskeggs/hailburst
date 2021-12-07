#include <endian.h>
#include <inttypes.h>

#include <hal/atomic.h>
#include <hal/thread.h>
#include <hal/watchdog.h>
#include <fsw/clock.h>
#include <fsw/debug.h>
#include <fsw/tlm.h>

enum {
    TLM_MAX_ASYNC_CLIENT_BUFFERS = 128,
    TLM_MAX_ASYNC_SIZE           = 16,
    TLM_MAX_SYNC_BUFFERS         = 1,
    TLM_MAX_SYNC_SIZE            = 64 * 1024,
};

typedef struct {
    uint32_t telemetry_id;
    uint32_t data_len;
    uint8_t  data_bytes[TLM_MAX_ASYNC_SIZE];
} tlm_async_t;

typedef struct {
    uint32_t telemetry_id;
    uint64_t timestamp_ns;
    uint8_t  data_bytes[TLM_MAX_SYNC_SIZE];
} tlm_sync_t;

struct {
    bool initialized;

    semaphore_t         wakeup;
    multichart_server_t async_chart;
    uint32_t            async_dropped; // atomic
    wall_t              sync_wall;
    thread_t            thread;

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

static void *telemetry_mainloop(void *encoder_opaque);

static void telemetry_mainloop_notify(void *opaque) {
    (void) opaque;

    // wake up main loop; fine for this to fail, because that just means there's already a wakeup pending.
    (void) semaphore_give(&telemetry.wakeup);
}

void telemetry_init(comm_enc_t *encoder) {
    assert(!telemetry.initialized);

    // set up message paths
    semaphore_init(&telemetry.wakeup);
    multichart_init_server(&telemetry.async_chart, sizeof(tlm_async_t), telemetry_mainloop_notify, NULL);
    wall_init(&telemetry.sync_wall, telemetry_mainloop_notify, NULL);

    telemetry.comm_encoder = encoder;

    telemetry.initialized = true;

    thread_create(&telemetry.thread, "tlm_mainloop", PRIORITY_WORKERS, telemetry_mainloop, NULL, RESTARTABLE);
}

static void tlm_async_notify(void *opaque) {
    (void) opaque;
    // no notification needs to be sent; asynchronous telemetry messages do not block
}

void tlm_async_init(tlm_async_endpoint_t *tep) {
    assert(tep != NULL);
    assert(telemetry.initialized);

    multichart_init_client(&tep->client, &telemetry.async_chart, TLM_MAX_ASYNC_CLIENT_BUFFERS, tlm_async_notify, NULL);
}

static void tlm_sync_notify(void *opaque) {
    tlm_sync_endpoint_t *tep = (tlm_sync_endpoint_t *) opaque;
    assert(tep != NULL);

    // it's fine if we can't give the semaphore; that just means there's already a wakeup pending
    (void) semaphore_give(&tep->sync_wake);
}

void tlm_sync_init(tlm_sync_endpoint_t *tep) {
    assert(tep != NULL);
    assert(telemetry.initialized);

    semaphore_init(&tep->sync_wake);
    hole_init(&tep->sync_hole, sizeof(tlm_sync_t), &telemetry.sync_wall, tlm_sync_notify, tep);
}

static tlm_async_t *telemetry_async_start(tlm_async_endpoint_t *tep) {
    assert(tep != NULL);
    tlm_async_t *async = multichart_request_start(&tep->client);
    if (async == NULL) {
        // can be relaxed because we don't care about previous writes being retired first
        atomic_fetch_add_relaxed(telemetry.async_dropped, 1);
    }
    return async;
}

static void telemetry_async_send(tlm_async_endpoint_t *tep, tlm_async_t *async) {
    assert(tep != NULL && async != NULL);

    multichart_request_send(&tep->client, async);
}

static tlm_sync_t *telemetry_start_sync(tlm_sync_endpoint_t *tep) {
    assert(tep != NULL);
    tlm_sync_t *sync = hole_prepare(&tep->sync_hole);
    // should never contend for this, because we actually wait for it to send!
    assert(sync != NULL);
    return sync;
}

static void telemetry_record_sync(tlm_sync_endpoint_t *tep, tlm_sync_t *sync, size_t data_len) {
    assert(telemetry.initialized);

    sync->timestamp_ns = clock_timestamp();
    hole_send(&tep->sync_hole, data_len + offsetof(tlm_sync_t, data_bytes));

    // wait for request to complete
    while (hole_peek(&tep->sync_hole) != NULL) {
        semaphore_take(&tep->sync_wake);
    }
}

static void *telemetry_mainloop(void *opaque) {
    (void) opaque;

    assert(telemetry.comm_encoder != NULL);

    for (;;) {
        comm_packet_t packet;

        if (atomic_load_relaxed(telemetry.async_dropped) > 0) {
            // if we've been losing data from our ring buffer, we should probably report that first!

            // this fetches the lastest drop count and replaces it with zero by atomic binary-and with 0.
            uint32_t drop_count = atomic_fetch_and_relaxed(telemetry.async_dropped, 0);

            debugf(CRITICAL, "Telemetry dropped: MessagesLost=%u", drop_count);

            // convert to big-endian
            drop_count = htobe32(drop_count);

            // fill in telemetry packet
            packet.cmd_tlm_id   = TLM_DROPPED_TID;
            packet.timestamp_ns = clock_timestamp();
            packet.data_len     = sizeof(drop_count);
            packet.data_bytes   = (uint8_t*) &drop_count;

            // transmit this packet
            comm_enc_encode(telemetry.comm_encoder, &packet);

            // fall through here so that we actually transmit at least one real async packet per loop
            // (and that we don't just keep dropping everything)
        }

        // pull telemetry from multichart
        uint64_t timestamp_ns = 0;
        tlm_async_t *async_elm = multichart_reply_start(&telemetry.async_chart, &timestamp_ns);
        if (async_elm != NULL) {
            // convert async element to packet
            packet.cmd_tlm_id   = async_elm->telemetry_id;
            packet.timestamp_ns = timestamp_ns;
            packet.data_len     = async_elm->data_len;
            packet.data_bytes   = async_elm->data_bytes; // array->pointer

            // transmit this packet
            comm_enc_encode(telemetry.comm_encoder, &packet);

            multichart_reply_send(&telemetry.async_chart, async_elm);

            watchdog_ok(WATCHDOG_ASPECT_TELEMETRY);
        } else {
            // pull synchronous telemetry from wall
            size_t sync_size = 0;
            const tlm_sync_t *sync_elm = wall_query(&telemetry.sync_wall, &sync_size);
            if (sync_elm != NULL) {
                assert(sync_size >= offsetof(tlm_sync_t, data_bytes));

                // convert sync element to packet
                packet.cmd_tlm_id   = sync_elm->telemetry_id;
                packet.timestamp_ns = sync_elm->timestamp_ns;
                packet.data_len     = sync_size - offsetof(tlm_sync_t, data_bytes);
                packet.data_bytes   = sync_elm->data_bytes;

                // transmit this packet
                comm_enc_encode(telemetry.comm_encoder, &packet);

                // let the synchronous sender know they can continue
                wall_reply(&telemetry.sync_wall, sync_elm);
            } else {
                // no asynchronous telemetry AND no synchronous telemetry... time to sleep!
                semaphore_take(&telemetry.wakeup);
            }
        }
    }
}

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
    if (num_readings * 14 > hole_max_msg_size(&tep->sync_hole)) {
        num_readings = hole_max_msg_size(&tep->sync_hole) / 14;
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