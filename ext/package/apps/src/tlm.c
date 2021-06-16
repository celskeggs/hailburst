#include <arpa/inet.h>
#include <assert.h>
#include <inttypes.h>

#include "clock.h"
#include "thread.h"
#include "tlm.h"

#define MAX_TLM_BODY (16)
#define MAX_BUFFERED (1024)

typedef struct {
    uint32_t telemetry_id;
    uint64_t timestamp_ns;
    uint8_t data_len; // make this larger if MAX_TLM_BODY gets bigger than 255
    uint8_t data_bytes[MAX_TLM_BODY];
} tlm_elem_t;

enum {
	CMD_RECEIVED_TID          = 0x01000001,
	CMD_COMPLETED_TID         = 0x01000002,
	CMD_NOT_RECOGNIZED_TID    = 0x01000003,
	TLM_DROPPED_TID           = 0x01000004,
	PONG_TID                  = 0x01000005,
	CLOCK_CALIBRATED_TID      = 0x01000006,
	MAG_PWR_STATE_CHANGED_TID = 0x02000001,
	MAG_READINGS_ARRAY_TID    = 0x02000002,
};

static bool telemetry_initialized = false;
static ringbuf_t telemetry_ring;
// atomic
static uint32_t telemetry_dropped = 0;

static pthread_t telemetry_mainloop_thread;

static void *telemetry_mainloop(void *encoder_opaque);

void telemetry_init(comm_enc_t *encoder) {
    assert(!telemetry_initialized);
    ringbuf_init(&telemetry_ring, MAX_BUFFERED, sizeof(tlm_elem_t));

    telemetry_initialized = true;

    thread_create(&telemetry_mainloop_thread, telemetry_mainloop, encoder);
}

static void telemetry_record(tlm_elem_t *insert) {
    size_t written = 0;
    assert(insert->data_len <= MAX_TLM_BODY);
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

static void *telemetry_mainloop(void *encoder_opaque) {
    comm_enc_t *encoder = (comm_enc_t *) encoder_opaque;
    tlm_elem_t local_element;
    comm_packet_t packet;
    uint32_t drop_count;
    for (;;) {
        if (telemetry_dropped > 0) {
            // if we've been losing data from our ring buffer, we should probably report that first!

            // this fetches the lastest drop count and replaces it with zero by atomic binary-and with 0.
            drop_count = __sync_fetch_and_and(&telemetry_dropped, 0);

            printf("Telemetry dropped: MessagesLost=%u\n", drop_count);

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

            // convert to packet
            packet.cmd_tlm_id = local_element.telemetry_id;
            packet.timestamp_ns = local_element.timestamp_ns;
            packet.data_len = local_element.data_len;
            packet.data_bytes = local_element.data_bytes; // array->pointer
        }

        // write to output
        comm_enc_encode(encoder, &packet);
    }
}

void tlm_cmd_received(uint64_t original_timestamp, uint32_t original_command_id) {
    printf("Command Received: OriginalTimestamp=%"PRIu64" OriginalCommandId=%08x\n",
           original_timestamp, original_command_id);

    tlm_elem_t tlm = { .telemetry_id = CMD_RECEIVED_TID, .data_len = 12 };
    uint32_t *out = (uint32_t*) tlm.data_bytes;
    *out++ = htonl((uint32_t) (original_timestamp >> 32));
    *out++ = htonl((uint32_t) original_timestamp);
    *out++ = htonl(original_command_id);
    telemetry_record(&tlm);
}

void tlm_cmd_completed(uint64_t original_timestamp, uint32_t original_command_id, bool success) {
    printf("Command Completed: OriginalTimestamp=%"PRIu64" OriginalCommandId=%08x Success=%d\n",
           original_timestamp, original_command_id, success);

    tlm_elem_t tlm = { .telemetry_id = CMD_COMPLETED_TID, .data_len = 13 };
    uint32_t *out = (uint32_t*) tlm.data_bytes;
    *out++ = htonl((uint32_t) (original_timestamp >> 32));
    *out++ = htonl((uint32_t) original_timestamp);
    *out++ = htonl(original_command_id);
    tlm.data_bytes[12] = (success ? 1 : 0);
    telemetry_record(&tlm);
}

void tlm_cmd_not_recognized(uint64_t original_timestamp, uint32_t original_command_id, uint32_t length) {
    printf("Command Not Recognized: OriginalTimestamp=%"PRIu64" OriginalCommandId=%08x Length=%u\n",
           original_timestamp, original_command_id, length);

    tlm_elem_t tlm = { .telemetry_id = CMD_NOT_RECOGNIZED_TID, .data_len = 16 };
    uint32_t *out = (uint32_t*) tlm.data_bytes;
    *out++ = htonl((uint32_t) (original_timestamp >> 32));
    *out++ = htonl((uint32_t) original_timestamp);
    *out++ = htonl(original_command_id);
    *out++ = htonl(length);
    telemetry_record(&tlm);
}

void tlm_pong(uint32_t ping_id) {
    printf("Pong: PingId=%08x\n", ping_id);

    tlm_elem_t tlm = { .telemetry_id = PONG_TID, .data_len = 4 };
    uint32_t *out = (uint32_t*) tlm.data_bytes;
    *out++ = htonl(ping_id);
    telemetry_record(&tlm);
}

void tlm_clock_calibrated(int64_t adjustment) {
    printf("ClockCalibrated: Adjustment=%"PRId64"\n", adjustment);

    tlm_elem_t tlm = { .telemetry_id = CLOCK_CALIBRATED_TID, .data_len = 8 };
    uint32_t *out = (uint32_t*) tlm.data_bytes;
    *out++ = htonl((uint32_t) (adjustment >> 32));
    *out++ = htonl((uint32_t) (adjustment >> 0));
    telemetry_record(&tlm);
}

void tlm_mag_pwr_state_changed(bool power_state) {
    printf("Magnetometer Power State Changed: PowerState=%d\n", power_state);

    tlm_elem_t tlm = { .telemetry_id = MAG_PWR_STATE_CHANGED_TID, .data_len = 1 };
    tlm.data_bytes[0] = (power_state ? 1 : 0);
    telemetry_record(&tlm);
}

void tlm_mag_readings_array(tlm_mag_reading_t *readings, size_t num_readings) {
    assert(num_readings == 1); // other counts not yet implemented because of needed ring buffer changes

    printf("Magnetometer Readings Array: Readings[0]={%"PRIu64", %d, %d, %d}\n",
           readings[0].reading_time, readings[0].mag_x, readings[0].mag_y, readings[0].mag_z);

    tlm_elem_t tlm = { .telemetry_id = MAG_READINGS_ARRAY_TID, .data_len = 14 };
    uint32_t *out = (uint32_t*) tlm.data_bytes;
    *out++ = htonl((uint32_t) (readings[0].reading_time >> 32));
    *out++ = htonl((uint32_t) (readings[0].reading_time >> 0));
    uint16_t *out16 = (uint16_t*) out;
    *out16++ = readings[0].mag_x;
    *out16++ = readings[1].mag_x;
    *out16++ = readings[2].mag_x;
    assert((uint8_t*) out16 - tlm.data_bytes == tlm.data_len);
    telemetry_record(&tlm);
}
