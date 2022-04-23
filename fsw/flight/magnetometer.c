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

static void magnetometer_telem_iterator_fetch(void *mr_opaque, size_t index, tlm_mag_reading_t *reading_out) {
    magnetometer_replica_t *mr = (magnetometer_replica_t *) mr_opaque;
    assert(mr != NULL && reading_out != NULL);

    tlm_mag_reading_t *reading = circ_buf_read_peek(mr->readings, index);
    assert(reading != NULL);
    *reading_out = *reading;
}

void magnetometer_clip(magnetometer_replica_t *mr) {
    assert(mr != NULL && mr->mut != NULL);

    local_time_t now = timer_epoch_ns();

    if (clip_is_restart()) {
        mr->mut->state = MS_INACTIVE;
        mr->mut->next_reading_time = 0;
        mr->mut->actual_reading_time = 0;
        mr->mut->check_latch_time = 0;
        circ_buf_reset(mr->readings);

        // make sure this can't get corrupted to a value that prevents us from sending telemetry
        if (mr->mut->last_telem_time > now) {
            mr->mut->last_telem_time = now;
        }
    }

    // scratch variables for use in switch statements
    rmap_status_t status;

    uint16_t single_value;
    uint16_t registers[4];

    rmap_txn_t rmap_txn;
    rmap_epoch_prepare(&rmap_txn, mr->endpoint);
    tlm_txn_t telem;
    telemetry_prepare(&telem, mr->telemetry_async, mr->replica_id);

    switch (mr->mut->state) {
    case MS_ACTIVATING:
        status = rmap_write_complete(&rmap_txn, NULL);
        if (status == RS_OK) {
            mr->mut->state = MS_ACTIVE;
            tlm_mag_pwr_state_changed(&telem, true);
        } else {
            debugf(WARNING, "Failed to turn on magnetometer power, error=0x%03x", status);
        }
        break;
    case MS_DEACTIVATING:
        status = rmap_write_complete(&rmap_txn, NULL);
        if (status == RS_OK) {
            mr->mut->state = MS_INACTIVE;
            tlm_mag_pwr_state_changed(&telem, false);
        } else {
            debugf(WARNING, "Failed to turn off magnetometer power, error=0x%03x", status);
        }
        break;
    case MS_LATCHING_ON:
        // TODO: stop after a certain number of retries?
        mr->mut->actual_reading_time = 0;
        status = rmap_write_complete(&rmap_txn, &mr->mut->actual_reading_time);
        if (status == RS_OK) {
            assert(mr->mut->actual_reading_time != 0);
            mr->mut->state = MS_LATCHED_ON;
            mr->mut->check_latch_time = now + LATCHING_DELAY_NS;
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
                tlm_mag_reading_t *reading = circ_buf_write_peek(mr->readings, 0);
                if (reading != NULL) {
                    reading->reading_time = clock_mission_adjust(mr->mut->actual_reading_time);
                    reading->mag_x = registers[REG_X - REG_LATCH];
                    reading->mag_y = registers[REG_Y - REG_LATCH];
                    reading->mag_z = registers[REG_Z - REG_LATCH];
                    circ_buf_write_done(mr->readings, 1);
                }
                mr->mut->state = MS_ACTIVE;
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
    uint8_t *command_bytes = command_receive(mr->command_endpoint, mr->replica_id, &command_length);
    if (command_bytes != NULL) {
        if (command_length == 1 && (command_bytes[0] == 0 || command_bytes[0] == 1)) {
            mr->mut->should_be_powered = (command_bytes[0] == 1);
            debugf(DEBUG, "Command set magnetometer power state to %u.", mr->mut->should_be_powered);
            command_reply(mr->command_endpoint, mr->replica_id, &telem, CMD_STATUS_OK);
        } else {
            // wrong length or invalid byte
            command_reply(mr->command_endpoint, mr->replica_id, &telem, CMD_STATUS_UNRECOGNIZED);
        }
    }

    if ((mr->mut->state == MS_INACTIVE || mr->mut->state == MS_DEACTIVATING) && mr->mut->should_be_powered) {
        debugf(DEBUG, "Turning on magnetometer power...");
        mr->mut->state = MS_ACTIVATING;
    } else if ((mr->mut->state == MS_ACTIVATING || mr->mut->state == MS_ACTIVE) && !mr->mut->should_be_powered) {
        debugf(DEBUG, "Turning off magnetometer power...");
        mr->mut->state = MS_DEACTIVATING;
    } else if (mr->mut->state == MS_ACTIVE && timer_epoch_ns() >= mr->mut->next_reading_time) {
        debugf(DEBUG, "Taking magnetometer reading...");
        mr->mut->state = MS_LATCHING_ON;
        mr->mut->next_reading_time += READING_DELAY_NS;
    } else if (mr->mut->state == MS_LATCHED_ON && now >= mr->mut->check_latch_time) {
        mr->mut->state = MS_TAKING_READING;
    }

    switch (mr->mut->state) {
    case MS_ACTIVATING:
        single_value = htobe16(POWER_ON);
        rmap_write_start(&rmap_txn, 0x00, REG_POWER, (uint8_t*) &single_value, sizeof(single_value));
        // we set this here -- rather than next cycle -- so that we can avoid the single-epoch discrepancy a delay
        // would imply
        mr->mut->next_reading_time = timer_epoch_ns() + READING_DELAY_NS;
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

    tlm_txn_t telem_synch;
    telemetry_prepare(&telem_synch, mr->telemetry_sync, mr->replica_id);

    circ_index_t downlink_count = circ_buf_read_avail(mr->readings);
    if (downlink_count == 0) {
        // nothing to downlink, so a send is unnecessary.
        mr->mut->last_telem_time = now;
    } else {
        // something to downlink... but only downlink at most every 5.5 seconds (to meet requirements), and only if
        // there's room in the telemetry buffer to actually transmit data.
        if (now >= mr->mut->last_telem_time + (uint64_t) 5500 * CLOCK_NS_PER_MS && telemetry_can_send(&telem_synch)) {
            size_t write_count = downlink_count;
            tlm_mag_readings_map(&telem_synch, &write_count, magnetometer_telem_iterator_fetch, (void *) mr);
            assert(write_count >= 1 && write_count <= downlink_count);
            circ_buf_read_done(mr->readings, write_count);

            mr->mut->last_telem_time = now;
        }
    }

    telemetry_commit(&telem_synch);
}
