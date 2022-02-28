#include <endian.h>
#include <inttypes.h>
#include <string.h>

#include <hal/atomic.h>
#include <hal/clock.h>
#include <hal/debug.h>
#include <hal/thread.h>
#include <synch/retry.h>
#include <flight/magnetometer.h>
#include <flight/telemetry.h>

enum {
    REG_ERRORS = 0,
    REG_POWER  = 1,
    REG_LATCH  = 2,
    REG_X      = 3,
    REG_Y      = 4,
    REG_Z      = 5,

    POWER_OFF = 0,
    POWER_ON  = 1,

    LATCH_OFF = 0,
    LATCH_ON  = 1,

    READING_DELAY_NS = 100 * 1000 * 1000, // take a reading every 100 ms
    LATCHING_DELAY_NS = 15 * 1000 * 1000, // wait 15 ms before checking for reading completion
};

void magnetometer_query_clip(magnetometer_t *mag) {
    assert(mag != NULL);

    if (clip_is_restart()) {
        mag->state = MS_INACTIVE;
        mag->next_reading_time = 0;
        mag->actual_reading_time = 0;
        mag->check_latch_time = 0;
    }

    // scratch variables for use in switch statements
    rmap_status_t status;

    uint16_t single_value;
    uint16_t registers[4];

    rmap_txn_t rmap_txn;
    rmap_epoch_prepare(&rmap_txn, mag->endpoint);

    switch (mag->state) {
    case MS_ACTIVATING:
        status = rmap_write_complete(&rmap_txn, NULL);
        if (status == RS_OK) {
            mag->state = MS_ACTIVE;
            mag->next_reading_time = clock_timestamp_monotonic() + READING_DELAY_NS;
            tlm_mag_pwr_state_changed(mag->telemetry_async, true);
        } else {
            debugf(WARNING, "Failed to turn on magnetometer power, error=0x%03x", status);
        }
        break;
    case MS_DEACTIVATING:
        status = rmap_write_complete(&rmap_txn, NULL);
        if (status == RS_OK) {
            mag->state = MS_INACTIVE;
            tlm_mag_pwr_state_changed(mag->telemetry_async, false);
        } else {
            debugf(WARNING, "Failed to turn off magnetometer power, error=0x%03x", status);
        }
        break;
    case MS_LATCHING_ON:
        // TODO: stop after a certain number of retries?
        mag->actual_reading_time = 0;
        status = rmap_write_complete(&rmap_txn, &mag->actual_reading_time);
        if (status == RS_OK) {
            assert(mag->actual_reading_time != 0);
            mag->state = MS_LATCHED_ON;
            mag->check_latch_time = clock_timestamp_monotonic() + LATCHING_DELAY_NS;
        } else {
            debugf(WARNING, "Failed to turn on magnetometer latch, error=0x%03x", status);
        }
        break;
    case MS_TAKING_READING:
        // TODO: stop after a certain number of retries?
        status = rmap_read_complete(&rmap_txn, (uint8_t*) registers, sizeof(registers), NULL);
        if (status == RS_OK) {
            for (int i = 0; i < 4; i++) {
                registers[i] = be16toh(registers[i]);
            }
            if (registers[0] == LATCH_OFF) {
                tlm_mag_reading_t *reading = chart_request_start(mag->readings);
                if (reading != NULL) {
                    reading->reading_time = mag->actual_reading_time;
                    reading->mag_x = registers[REG_X - REG_LATCH];
                    reading->mag_y = registers[REG_Y - REG_LATCH];
                    reading->mag_z = registers[REG_Z - REG_LATCH];
                    chart_request_send(mag->readings, 1);
                }
                mag->state = MS_ACTIVE;
            }
            // otherwise keep checking until latch turns off
        } else {
            debugf(WARNING, "Failed to turn on magnetometer latch, error=0x%03x", status);
        }
        break;
    default:
        // nothing to be received
        break;
    }

    if ((mag->state == MS_INACTIVE || mag->state == MS_DEACTIVATING)
                    && atomic_load_relaxed(mag->should_be_powered)) {
        debugf(DEBUG, "Turning on magnetometer power...");
        mag->state = MS_ACTIVATING;
    } else if ((mag->state == MS_ACTIVATING || mag->state == MS_ACTIVE)
                    && !atomic_load_relaxed(mag->should_be_powered)) {
        debugf(DEBUG, "Turning off magnetometer power...");
        mag->state = MS_DEACTIVATING;
    } else if (mag->state == MS_ACTIVE && clock_timestamp_monotonic() >= mag->next_reading_time) {
        debugf(DEBUG, "Taking magnetometer reading...");
        mag->state = MS_LATCHING_ON;
        mag->next_reading_time += READING_DELAY_NS;
    } else if (mag->state == MS_LATCHED_ON && clock_timestamp_monotonic() >= mag->check_latch_time) {
        mag->state = MS_TAKING_READING;
    }

    switch (mag->state) {
    case MS_ACTIVATING:
        single_value = htobe16(POWER_ON);
        rmap_write_start(&rmap_txn, 0x00, REG_POWER, (uint8_t*) &single_value, sizeof(single_value));
        break;
    case MS_DEACTIVATING:
        single_value = htobe16(POWER_OFF);
        rmap_write_start(&rmap_txn, 0x00, REG_POWER, (uint8_t*) &single_value, sizeof(single_value));
        break;
    case MS_LATCHING_ON:
        single_value = htobe16(LATCH_ON);
        rmap_write_start(&rmap_txn, 0x00, REG_LATCH, (uint8_t*) &single_value, sizeof(single_value));
        break;
    case MS_TAKING_READING:
        rmap_read_start(&rmap_txn, 0x00, REG_LATCH, sizeof(uint16_t) * 4);
        static_assert(REG_LATCH + 1 == REG_X, "assumptions about register layout");
        static_assert(REG_LATCH + 2 == REG_Y, "assumptions about register layout");
        static_assert(REG_LATCH + 3 == REG_Z, "assumptions about register layout");
        break;
    default:
        // nothing to be transmitted
        break;
    }

    rmap_epoch_commit(&rmap_txn);
}

static void magnetometer_telem_iterator_fetch(void *mag_opaque, size_t index, tlm_mag_reading_t *reading_out) {
    magnetometer_t *mag = (magnetometer_t *) mag_opaque;
    assert(mag != NULL && reading_out != NULL);

    *reading_out = *(tlm_mag_reading_t*) chart_reply_peek(mag->readings, index);
}

void magnetometer_telem_loop(magnetometer_t *mag) {
    assert(mag != NULL);

    // runs every 5.5 seconds to meet requirements
    for (;;) {
        uint64_t last_telem_time = clock_timestamp_monotonic();

        // see if we have readings to downlink
        size_t downlink_count = chart_reply_avail(mag->readings);
        if (downlink_count > 0) {
            size_t write_count = downlink_count;
            tlm_sync_mag_readings_map(mag->telemetry_sync, &write_count, magnetometer_telem_iterator_fetch, mag);
            assert(write_count >= 1 && write_count <= downlink_count);
            chart_reply_send(mag->readings, write_count);
        }

        task_delay_abs(last_telem_time + (uint64_t) 5500000000);
    }
}

void magnetometer_set_powered(magnetometer_t *mag, bool powered) {
    assert(mag != NULL);
    if (powered != mag->should_be_powered) {
        debugf(DEBUG, "Notifying mag_query_loop about new requested power state: %u.", powered);
        atomic_store_relaxed(mag->should_be_powered, powered);
    }
}
