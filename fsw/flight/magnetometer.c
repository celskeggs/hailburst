#include <endian.h>
#include <inttypes.h>
#include <string.h>

#include <hal/debug.h>
#include <flight/clock.h>
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
        circ_buf_reset(mag->readings);
    }

    // scratch variables for use in switch statements
    rmap_status_t status;

    uint16_t single_value;
    uint16_t registers[4];

    rmap_txn_t rmap_txn;
    rmap_epoch_prepare(&rmap_txn, mag->endpoint);
    tlm_txn_t telem;
    telemetry_prepare(&telem, mag->telemetry_async, MAGNETOMETER_REPLICA_ID);

    switch (mag->state) {
    case MS_ACTIVATING:
        status = rmap_write_complete(&rmap_txn, NULL);
        if (status == RS_OK) {
            mag->state = MS_ACTIVE;
            mag->next_reading_time = timer_now_ns() + READING_DELAY_NS;
            tlm_mag_pwr_state_changed(&telem, true);
        } else {
            debugf(WARNING, "Failed to turn on magnetometer power, error=0x%03x", status);
        }
        break;
    case MS_DEACTIVATING:
        status = rmap_write_complete(&rmap_txn, NULL);
        if (status == RS_OK) {
            mag->state = MS_INACTIVE;
            tlm_mag_pwr_state_changed(&telem, false);
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
            mag->check_latch_time = timer_now_ns() + LATCHING_DELAY_NS;
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
                tlm_mag_reading_t *reading = circ_buf_write_peek(mag->readings, 0);
                if (reading != NULL) {
                    reading->reading_time = clock_mission_adjust(mag->actual_reading_time);
                    reading->mag_x = registers[REG_X - REG_LATCH];
                    reading->mag_y = registers[REG_Y - REG_LATCH];
                    reading->mag_z = registers[REG_Z - REG_LATCH];
                    circ_buf_write_done(mag->readings, 1);
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

    size_t command_length = 0;
    uint8_t *command_bytes = command_receive(mag->command_endpoint, MAGNETOMETER_REPLICA_ID, &command_length);
    if (command_bytes != NULL) {
        if (command_length == 1 && (command_bytes[0] == 0 || command_bytes[0] == 1)) {
            mag->should_be_powered = (command_bytes[0] == 1);
            debugf(DEBUG, "Command set magnetometer power state to %u.", mag->should_be_powered);
            command_reply(mag->command_endpoint, MAGNETOMETER_REPLICA_ID, &telem, CMD_STATUS_OK);
        } else {
            // wrong length or invalid byte
            command_reply(mag->command_endpoint, MAGNETOMETER_REPLICA_ID, &telem, CMD_STATUS_UNRECOGNIZED);
        }
    }

    if ((mag->state == MS_INACTIVE || mag->state == MS_DEACTIVATING) && mag->should_be_powered) {
        debugf(DEBUG, "Turning on magnetometer power...");
        mag->state = MS_ACTIVATING;
    } else if ((mag->state == MS_ACTIVATING || mag->state == MS_ACTIVE) && !mag->should_be_powered) {
        debugf(DEBUG, "Turning off magnetometer power...");
        mag->state = MS_DEACTIVATING;
    } else if (mag->state == MS_ACTIVE && timer_now_ns() >= mag->next_reading_time) {
        debugf(DEBUG, "Taking magnetometer reading...");
        mag->state = MS_LATCHING_ON;
        mag->next_reading_time += READING_DELAY_NS;
    } else if (mag->state == MS_LATCHED_ON && timer_now_ns() >= mag->check_latch_time) {
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

    telemetry_commit(&telem);
    rmap_epoch_commit(&rmap_txn);
}

static void magnetometer_telem_iterator_fetch(void *mag_opaque, size_t index, tlm_mag_reading_t *reading_out) {
    magnetometer_t *mag = (magnetometer_t *) mag_opaque;
    assert(mag != NULL && reading_out != NULL);

    tlm_mag_reading_t *reading = circ_buf_read_peek(mag->readings, index);
    assert(reading != NULL);
    *reading_out = *reading;
}

void magnetometer_telem_clip(magnetometer_t *mag) {
    assert(mag != NULL);

    local_time_t now = timer_now_ns();

    if (clip_is_restart()) {
        circ_buf_reset(mag->readings);
        // make sure this can't get corrupted to a value that prevents us from sending telemetry
        if (mag->last_telem_time > now) {
            mag->last_telem_time = now;
        }
    }

    tlm_txn_t telem;
    telemetry_prepare(&telem, mag->telemetry_sync, MAGNETOMETER_REPLICA_ID);

    circ_index_t downlink_count = circ_buf_read_avail(mag->readings);
    if (downlink_count == 0) {
        // nothing to downlink, so a send is unnecessary.
        mag->last_telem_time = now;
    } else {
        // something to downlink... but only downlink at most every 5.5 seconds (to meet requirements), and only if
        // there's room in the telemetry buffer to actually transmit data.
        if (now >= mag->last_telem_time + (uint64_t) 5500 * CLOCK_NS_PER_MS && telemetry_can_send(&telem)) {
            size_t write_count = downlink_count;
            tlm_mag_readings_map(&telem, &write_count, magnetometer_telem_iterator_fetch, mag);
            assert(write_count >= 1 && write_count <= downlink_count);
            circ_buf_read_done(mag->readings, write_count);

            mag->last_telem_time = now;
        }
    }

    telemetry_commit(&telem);
}
