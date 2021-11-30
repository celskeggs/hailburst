#include <endian.h>
#include <inttypes.h>

#include <hal/atomic.h>
#include <hal/thread.h>
#include <hal/watchdog.h>
#include <fsw/clock.h>
#include <fsw/debug.h>
#include <fsw/tlm.h>

enum {
    TLM_MAX_ASYNC_BUFFERS = 1024,
    TLM_MAX_ASYNC_SIZE    = 16,
    TLM_MAX_SYNC_BUFFERS  = 1,
    TLM_MAX_SYNC_SIZE     = 64 * 1024,
};

typedef struct {
    uint32_t telemetry_id; // can be TLM_SYNC_MARKER to indicate that this is just a wakeup
    uint64_t timestamp_ns;
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

    queue_t  async_queue;
    uint32_t async_dropped; // atomic
    wall_t   sync_wall;
    thread_t thread;

    comm_enc_t *comm_encoder;
} telemetry = {
    .initialized   = false,
    .async_dropped = 0,
};

enum {
    // indicates that this is a wakeup notification for the mainloop to check on the sync wall
    TLM_SYNC_TID = 0xFFFFFFFF,

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

static void telemetry_wall_notify(void *opaque) {
    (void) opaque;

    // we only send a TLM_SYNC_TID notification if the async queue is empty.
    // otherwise, the main loop will be woken up anyway.
    if (queue_is_empty(&telemetry.async_queue)) {
        tlm_async_t tlm = {
            .telemetry_id = TLM_SYNC_TID,
        };
        // no big deal if we can't send; that just means that there's a wakeup pending.
        (void) queue_send_try(&telemetry.async_queue, &tlm);
    }
}

void telemetry_init(comm_enc_t *encoder) {
    assert(!telemetry.initialized);

    // set up message paths
    queue_init(&telemetry.async_queue, sizeof(tlm_async_t), TLM_MAX_ASYNC_BUFFERS);
    wall_init(&telemetry.sync_wall, telemetry_wall_notify, NULL);

    telemetry.comm_encoder = encoder;

    telemetry.initialized = true;

    thread_create(&telemetry.thread, "tlm_mainloop", PRIORITY_WORKERS, telemetry_mainloop, NULL, RESTARTABLE);
}

static void telemetry_record_async(tlm_async_t *insert) {
    bool written = false;
    assert(insert->data_len <= TLM_MAX_ASYNC_SIZE);
    if (telemetry.initialized) {
        // first, snapshot current time
        insert->timestamp_ns = clock_timestamp();
        // then write element to ring buffer without blocking
        written = queue_send_try(&telemetry.async_queue, insert);
    }
    if (!written) {
        // can be relaxed because we don't care about previous writes being retired first
        atomic_fetch_add_relaxed(telemetry.async_dropped, 1);
    }
}

static void telemetry_hole_notify(void *opaque) {
    tlm_sync_endpoint_t *tep = (tlm_sync_endpoint_t *) opaque;
    assert(tep != NULL);

    // it's fine if we can't give the semaphore; that just means there's already a wakeup pending
    (void) semaphore_give(&tep->sync_wake);
}

void tlm_sync_init(tlm_sync_endpoint_t *tep) {
    assert(tep != NULL);
    assert(telemetry.initialized);

    semaphore_init(&tep->sync_wake);
    hole_init(&tep->sync_hole, sizeof(tlm_sync_t), &telemetry.sync_wall, telemetry_hole_notify, tep);
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

            debugf("Telemetry dropped: MessagesLost=%u", drop_count);

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

        tlm_async_t async_elm;

        // pull telemetry from ring buffer
        if (!queue_recv_try(&telemetry.async_queue, &async_elm)) {
            // nothing immediately ready, so let's check on the wall before we sleep

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

                // back to the top; see what's available now
                continue;
            }

            // nothing on the wall, so go to sleep until we have something
            queue_recv(&telemetry.async_queue, &async_elm);
        }

        if (async_elm.telemetry_id == TLM_SYNC_TID) {
            // this just happens to wake us up to remind us to pull from the sync wall. go back to the top and recheck.
            continue;
        }

        // this is the case where we have an async telemetry element to process

        // convert async element to packet
        packet.cmd_tlm_id   = async_elm.telemetry_id;
        packet.timestamp_ns = async_elm.timestamp_ns;
        packet.data_len     = async_elm.data_len;
        packet.data_bytes   = async_elm.data_bytes; // array->pointer

        // transmit this packet
        comm_enc_encode(telemetry.comm_encoder, &packet);

        watchdog_ok(WATCHDOG_ASPECT_TELEMETRY);

        // go back around to see if we can transmit somethng else now
    }
}

void tlm_cmd_received(uint64_t original_timestamp, uint32_t original_command_id) {
    debugf("Command Received: OriginalTimestamp=%"PRIu64" OriginalCommandId=%08x",
           original_timestamp, original_command_id);

    tlm_async_t tlm = { .telemetry_id = CMD_RECEIVED_TID, .data_len = 12 };
    uint32_t *out = (uint32_t*) tlm.data_bytes;
    *out++ = htobe32((uint32_t) (original_timestamp >> 32));
    *out++ = htobe32((uint32_t) original_timestamp);
    *out++ = htobe32(original_command_id);
    telemetry_record_async(&tlm);
}

void tlm_cmd_completed(uint64_t original_timestamp, uint32_t original_command_id, bool success) {
    debugf("Command Completed: OriginalTimestamp=%"PRIu64" OriginalCommandId=%08x Success=%d",
           original_timestamp, original_command_id, success);

    tlm_async_t tlm = { .telemetry_id = CMD_COMPLETED_TID, .data_len = 13 };
    uint32_t *out = (uint32_t*) tlm.data_bytes;
    *out++ = htobe32((uint32_t) (original_timestamp >> 32));
    *out++ = htobe32((uint32_t) original_timestamp);
    *out++ = htobe32(original_command_id);
    tlm.data_bytes[12] = (success ? 1 : 0);
    telemetry_record_async(&tlm);
}

void tlm_cmd_not_recognized(uint64_t original_timestamp, uint32_t original_command_id, uint32_t length) {
    debugf("Command Not Recognized: OriginalTimestamp=%"PRIu64" OriginalCommandId=%08x Length=%u",
           original_timestamp, original_command_id, length);

    tlm_async_t tlm = { .telemetry_id = CMD_NOT_RECOGNIZED_TID, .data_len = 16 };
    uint32_t *out = (uint32_t*) tlm.data_bytes;
    *out++ = htobe32((uint32_t) (original_timestamp >> 32));
    *out++ = htobe32((uint32_t) original_timestamp);
    *out++ = htobe32(original_command_id);
    *out++ = htobe32(length);
    telemetry_record_async(&tlm);
}

void tlm_pong(uint32_t ping_id) {
    debugf("Pong: PingId=%08x", ping_id);

    tlm_async_t tlm = { .telemetry_id = PONG_TID, .data_len = 4 };
    uint32_t *out = (uint32_t*) tlm.data_bytes;
    *out++ = htobe32(ping_id);
    telemetry_record_async(&tlm);
}

void tlm_clock_calibrated(int64_t adjustment) {
    debugf("ClockCalibrated: Adjustment=%"PRId64"", adjustment);

    tlm_async_t tlm = { .telemetry_id = CLOCK_CALIBRATED_TID, .data_len = 8 };
    uint32_t *out = (uint32_t*) tlm.data_bytes;
    *out++ = htobe32((uint32_t) (adjustment >> 32));
    *out++ = htobe32((uint32_t) (adjustment >> 0));
    telemetry_record_async(&tlm);
}

void tlm_heartbeat(void) {
    debugf("Heartbeat");

    tlm_async_t tlm = { .telemetry_id = HEARTBEAT_TID, .data_len = 0 };
    telemetry_record_async(&tlm);
}

void tlm_mag_pwr_state_changed(bool power_state) {
    debugf("Magnetometer Power State Changed: PowerState=%d", power_state);

    tlm_async_t tlm = { .telemetry_id = MAG_PWR_STATE_CHANGED_TID, .data_len = 1 };
    tlm.data_bytes[0] = (power_state ? 1 : 0);
    telemetry_record_async(&tlm);
}

void tlm_sync_mag_readings_iterator(tlm_sync_endpoint_t *tep,
                                    bool (*iterator)(void *param, tlm_mag_reading_t *out), void *param) {

    // get the buffer
    tlm_sync_t *sync = telemetry_start_sync(tep);

    // now fill up the buffer
    sync->telemetry_id = MAG_READINGS_ARRAY_TID;
    uint16_t *out = (uint16_t*) sync->data_bytes;
    debugf("Magnetometer Readings Array:");
    size_t num_readings = 0;
    while ((uint8_t*) out - sync->data_bytes + 14 <= (ssize_t) hole_max_msg_size(&tep->sync_hole)) {
        tlm_mag_reading_t rd;

        if (!iterator(param, &rd)) {
            break;
        }

        debugf("  Readings[%zu]={%"PRIu64", %d, %d, %d}", num_readings, rd.reading_time, rd.mag_x, rd.mag_y, rd.mag_z);

        *out++ = htobe16((uint16_t) (rd.reading_time >> 48));
        *out++ = htobe16((uint16_t) (rd.reading_time >> 32));
        *out++ = htobe16((uint16_t) (rd.reading_time >> 16));
        *out++ = htobe16((uint16_t) (rd.reading_time >> 0));

        *out++ = htobe16(rd.mag_x);
        *out++ = htobe16(rd.mag_y);
        *out++ = htobe16(rd.mag_z);

        num_readings++;
    }
    debugf("  Total number of readings: %zu", num_readings);
    assert((uint8_t*) out - sync->data_bytes == (ssize_t) (num_readings * 14));

    // write the sync record to the ring buffer, and wait for it to be written out to the telemetry stream
    telemetry_record_sync(tep, sync, (uint8_t*) out - sync->data_bytes);
}
