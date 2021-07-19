#include <arpa/inet.h>
#include <assert.h>
#include <inttypes.h>

#include "clock.h"
#include "debug.h"
#include "thread.h"
#include "tlm.h"

#define MAX_TLM_BODY (16)
#define MAX_BUFFERED (1024)
#define LEN_MARKER_SYNC (0xFF)
#define SCRATCH_BUFFER_SIZE (64*1024)

typedef struct {
    uint32_t telemetry_id;
    uint64_t timestamp_ns;
    uint8_t data_len; // make this larger if MAX_TLM_BODY gets bigger than 255
    union {
        // if data_len is not LEN_MARKER_SYNC
        uint8_t data_bytes[MAX_TLM_BODY];
        // if data_len is LEN_MARKER_SYNC
        struct {
            size_t sync_data_len;
            uint8_t *sync_data_ptr;
            bool *sync_complete_flag;
        } sync;
    };
} tlm_elem_t;

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

static bool telemetry_initialized = false;
static ringbuf_t telemetry_ring;
// atomic
static uint32_t telemetry_dropped = 0;
// to protect sync flags
static pthread_mutex_t sync_flag_mutex;
static pthread_cond_t sync_flag_cond;
// to hold scratch buffers
static ringbuf_t scratch_buffers;

static pthread_t telemetry_mainloop_thread;

static void *telemetry_mainloop(void *encoder_opaque);

void telemetry_init(comm_enc_t *encoder) {
    assert(!telemetry_initialized);

    // set up ring buffer
    ringbuf_init(&telemetry_ring, MAX_BUFFERED, sizeof(tlm_elem_t));

    // set up sync flag infra
    mutex_init(&sync_flag_mutex);
    cond_init(&sync_flag_cond);

    // set up scratch buffer lending
    ringbuf_init(&scratch_buffers, 1, sizeof(uint8_t*));
    uint8_t *scratch_buf = malloc(SCRATCH_BUFFER_SIZE);
    assert(scratch_buf != NULL);
    size_t wc = ringbuf_write(&scratch_buffers, &scratch_buf, 1, RB_NONBLOCKING);
    assert(wc == 1);

    telemetry_initialized = true;

    thread_create(&telemetry_mainloop_thread, telemetry_mainloop, encoder);
}

static void telemetry_record_async(tlm_elem_t *insert) {
    size_t written = 0;
    assert(insert->data_len <= MAX_TLM_BODY && insert->data_len != LEN_MARKER_SYNC);
    if (telemetry_initialized) {
        // first, snapshot current time
        insert->timestamp_ns = clock_timestamp();
        // then write element to ring buffer
        written = ringbuf_write(&telemetry_ring, insert, 1, RB_NONBLOCKING);
        assert(written <= 1);
    }
    if (written != 1) {
        __sync_fetch_and_add(&telemetry_dropped, (uint32_t) 1);
    }
}

static void telemetry_record_sync(uint32_t telemetry_id, uint8_t *data_ptr, size_t data_len) {
    assert(telemetry_initialized);
    bool complete_flag = false;
    tlm_elem_t element = {
        .telemetry_id = telemetry_id,
        .timestamp_ns = clock_timestamp(),
        .data_len = LEN_MARKER_SYNC,
        .sync = {
            .sync_data_len = data_len,
            .sync_data_ptr = data_ptr,
            .sync_complete_flag = &complete_flag,
        },
    };
    // write sync element to ring buffer
    size_t written = ringbuf_write(&telemetry_ring, &element, 1, RB_BLOCKING);
    assert(written == 1);
    // wait for flag to be raised, so that we can proceed and potentially reuse *data_ptr
    mutex_lock(&sync_flag_mutex);
    while (complete_flag == false) {
        cond_wait(&sync_flag_cond, &sync_flag_mutex);
    }
    assert(complete_flag == true);
    mutex_unlock(&sync_flag_mutex);
}

static void *telemetry_mainloop(void *encoder_opaque) {
    comm_enc_t *encoder = (comm_enc_t *) encoder_opaque;
    tlm_elem_t local_element;
    comm_packet_t packet;
    uint32_t drop_count;
    bool *complete_flag;
    for (;;) {
        complete_flag = NULL;

        if (telemetry_dropped > 0) {
            // if we've been losing data from our ring buffer, we should probably report that first!

            // this fetches the lastest drop count and replaces it with zero by atomic binary-and with 0.
            drop_count = __sync_fetch_and_and(&telemetry_dropped, 0);

            debugf("Telemetry dropped: MessagesLost=%u", drop_count);

            // convert to big-endian
            drop_count = htonl(drop_count);

            // fill in telemetry packet
            packet.cmd_tlm_id = TLM_DROPPED_TID;
            packet.timestamp_ns = clock_timestamp();
            packet.data_len = sizeof(drop_count);
            packet.data_bytes = (uint8_t*) &drop_count;
        } else {
            // pull telemetry from ring buffer
            size_t count = ringbuf_read(&telemetry_ring, &local_element, 1, RB_BLOCKING);
            assert(count == 1);

            if (local_element.data_len == LEN_MARKER_SYNC) {
                // pull synchronous element
                packet.cmd_tlm_id = local_element.telemetry_id;
                packet.timestamp_ns = local_element.timestamp_ns;
                packet.data_len = local_element.sync.sync_data_len;
                packet.data_bytes = local_element.sync.sync_data_ptr;
                complete_flag = local_element.sync.sync_complete_flag;
                assert(complete_flag != NULL);
            } else {
                // convert async element to packet
                packet.cmd_tlm_id = local_element.telemetry_id;
                packet.timestamp_ns = local_element.timestamp_ns;
                packet.data_len = local_element.data_len;
                packet.data_bytes = local_element.data_bytes; // array->pointer
            }
        }

        // write to output
        comm_enc_encode(encoder, &packet);

        if (complete_flag) {
            // let the synchronous sender know they can reuse their memory
            mutex_lock(&sync_flag_mutex);
            assert(*complete_flag == false);
            *complete_flag = true;
            cond_broadcast(&sync_flag_cond);
            mutex_unlock(&sync_flag_mutex);
        }
    }
}

void tlm_cmd_received(uint64_t original_timestamp, uint32_t original_command_id) {
    debugf("Command Received: OriginalTimestamp=%"PRIu64" OriginalCommandId=%08x",
           original_timestamp, original_command_id);

    tlm_elem_t tlm = { .telemetry_id = CMD_RECEIVED_TID, .data_len = 12 };
    uint32_t *out = (uint32_t*) tlm.data_bytes;
    *out++ = htonl((uint32_t) (original_timestamp >> 32));
    *out++ = htonl((uint32_t) original_timestamp);
    *out++ = htonl(original_command_id);
    telemetry_record_async(&tlm);
}

void tlm_cmd_completed(uint64_t original_timestamp, uint32_t original_command_id, bool success) {
    debugf("Command Completed: OriginalTimestamp=%"PRIu64" OriginalCommandId=%08x Success=%d",
           original_timestamp, original_command_id, success);

    tlm_elem_t tlm = { .telemetry_id = CMD_COMPLETED_TID, .data_len = 13 };
    uint32_t *out = (uint32_t*) tlm.data_bytes;
    *out++ = htonl((uint32_t) (original_timestamp >> 32));
    *out++ = htonl((uint32_t) original_timestamp);
    *out++ = htonl(original_command_id);
    tlm.data_bytes[12] = (success ? 1 : 0);
    telemetry_record_async(&tlm);
}

void tlm_cmd_not_recognized(uint64_t original_timestamp, uint32_t original_command_id, uint32_t length) {
    debugf("Command Not Recognized: OriginalTimestamp=%"PRIu64" OriginalCommandId=%08x Length=%u",
           original_timestamp, original_command_id, length);

    tlm_elem_t tlm = { .telemetry_id = CMD_NOT_RECOGNIZED_TID, .data_len = 16 };
    uint32_t *out = (uint32_t*) tlm.data_bytes;
    *out++ = htonl((uint32_t) (original_timestamp >> 32));
    *out++ = htonl((uint32_t) original_timestamp);
    *out++ = htonl(original_command_id);
    *out++ = htonl(length);
    telemetry_record_async(&tlm);
}

void tlm_pong(uint32_t ping_id) {
    debugf("Pong: PingId=%08x", ping_id);

    tlm_elem_t tlm = { .telemetry_id = PONG_TID, .data_len = 4 };
    uint32_t *out = (uint32_t*) tlm.data_bytes;
    *out++ = htonl(ping_id);
    telemetry_record_async(&tlm);
}

void tlm_clock_calibrated(int64_t adjustment) {
    debugf("ClockCalibrated: Adjustment=%"PRId64"", adjustment);

    tlm_elem_t tlm = { .telemetry_id = CLOCK_CALIBRATED_TID, .data_len = 8 };
    uint32_t *out = (uint32_t*) tlm.data_bytes;
    *out++ = htonl((uint32_t) (adjustment >> 32));
    *out++ = htonl((uint32_t) (adjustment >> 0));
    telemetry_record_async(&tlm);
}

void tlm_heartbeat(void) {
    debug0("Heartbeat");

    tlm_elem_t tlm = { .telemetry_id = HEARTBEAT_TID, .data_len = 0 };
    telemetry_record_async(&tlm);
}

void tlm_mag_pwr_state_changed(bool power_state) {
    debugf("Magnetometer Power State Changed: PowerState=%d", power_state);

    tlm_elem_t tlm = { .telemetry_id = MAG_PWR_STATE_CHANGED_TID, .data_len = 1 };
    tlm.data_bytes[0] = (power_state ? 1 : 0);
    telemetry_record_async(&tlm);
}

void tlm_sync_mag_readings_array(tlm_mag_reading_t *readings, size_t num_readings) {
    assert(num_readings > 0);
    assert(num_readings * 14 <= SCRATCH_BUFFER_SIZE); // upper bound based on encoding capacity

    // wait until a buffer is available
    uint8_t *scratch_buf = NULL;
    size_t rioc = ringbuf_read(&scratch_buffers, &scratch_buf, 1, RB_BLOCKING);
    assert(rioc == 1 && scratch_buf != NULL);

    // now fill up the scratch buffer
    uint16_t *out = (uint16_t*) scratch_buf;
    debugf("Magnetometer Readings Array: %zu readings:", num_readings);
    for (size_t i = 0; i < num_readings; i++) {
        debugf("  Readings[%zu]={%"PRIu64", %d, %d, %d}",
            i, readings[i].reading_time, readings[i].mag_x, readings[i].mag_y, readings[i].mag_z);

        *out++ = htons((uint16_t) (readings[i].reading_time >> 48));
        *out++ = htons((uint16_t) (readings[i].reading_time >> 32));
        *out++ = htons((uint16_t) (readings[i].reading_time >> 16));
        *out++ = htons((uint16_t) (readings[i].reading_time >> 0));

        *out++ = htons(readings[i].mag_x);
        *out++ = htons(readings[i].mag_y);
        *out++ = htons(readings[i].mag_z);
    }
    assert((uint8_t*) out - scratch_buf == num_readings * 14);

    // write the sync record to the ring buffer, and wait for it to be written out to the telemetry stream
    telemetry_record_sync(MAG_READINGS_ARRAY_TID, scratch_buf, num_readings * 14);

    // now that we can reuse this scratch buffer, release it for the next client!
    rioc = ringbuf_write(&scratch_buffers, &scratch_buf, 1, RB_NONBLOCKING);
    assert(rioc == 1);
}
