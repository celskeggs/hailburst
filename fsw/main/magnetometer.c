#include <endian.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>

#include <hal/atomic.h>
#include <hal/thread.h>
#include <fsw/clock.h>
#include <fsw/debug.h>
#include <fsw/magnetometer.h>
#include <fsw/retry.h>
#include <fsw/tlm.h>

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

    TRANSACTION_RETRIES = 5,

    MAG_RS_NOT_ALIGNED   = 1,
    MAG_RS_INVALID_ADDR  = 2,
    MAG_RS_INVALID_VALUE = 3,
    MAG_RS_CORRUPT_DATA  = 4,

    MAGNETOMETER_MAX_READINGS = 100,

    READING_DELAY_NS = 100 * 1000 * 1000,
};

static bool magnetometer_set_register(magnetometer_t *mag, uint32_t reg, uint16_t value, uint64_t *ack_timestamp_out) {
    rmap_status_t status;
    value = htobe16(value);

    RETRY(TRANSACTION_RETRIES, "magnetometer register %u=%u set, error=0x%03x", reg, value, status) {
        status = rmap_write_exact(&mag->endpoint, &mag->address, RF_VERIFY | RF_ACKNOWLEDGE | RF_INCREMENT,
                                  0x00, reg, 2, (uint8_t *) &value, ack_timestamp_out);
        if (status == RS_OK) {
            return true;
        }
    }
    return false;
}

static void sleep_until(uint64_t target_time) {
    int64_t remain;
    while ((remain = target_time - clock_timestamp_monotonic()) > 0) {
        usleep(remain / 1000);
    }
}

static bool magnetometer_take_reading(magnetometer_t *mag, tlm_mag_reading_t *reading_out) {
    uint64_t reading_time = 0;
    // trigger reading
    if (!magnetometer_set_register(mag, REG_LATCH, LATCH_ON, &reading_time)) {
        return false;
    }
    if (reading_out != NULL) {
        reading_out->reading_time = reading_time;
    }

    usleep(15000);

    rmap_status_t status;
    for (int loop_retries = 0; loop_retries < 50; loop_retries++) {
        static_assert(REG_LATCH + 1 == REG_X, "assumptions about register layout");
        static_assert(REG_LATCH + 2 == REG_Y, "assumptions about register layout");
        static_assert(REG_LATCH + 3 == REG_Z, "assumptions about register layout");

        uint16_t registers[4];
        status = RS_INVALID_ERR;
        RETRY(TRANSACTION_RETRIES, "magnetometer register reading, error=0x%03x", status) {
            status = rmap_read_exact(&mag->endpoint, &mag->address, RF_INCREMENT,
                                     0x00, REG_LATCH, sizeof(registers), (uint8_t *) registers);
            if (status == RS_OK) {
                break;
            }
        }
        if (status != RS_OK) {
            return false;
        }

        for (int i = 0; i < 4; i++) {
            registers[i] = be16toh(registers[i]);
        }

        assert(registers[0] == LATCH_OFF || registers[0] == LATCH_ON);
        if (registers[0] == LATCH_OFF) {
            if (reading_out != NULL) {
                reading_out->mag_x = registers[REG_X - REG_LATCH];
                reading_out->mag_y = registers[REG_Y - REG_LATCH];
                reading_out->mag_z = registers[REG_Z - REG_LATCH];
            }
            return true;
        }

        usleep(200);
    }
    debugf(CRITICAL, "Magnetometer: ran out of loop retries while trying to take a reading.");
    return false;
}

static void magnetometer_mainloop(void *mag_opaque) {
    magnetometer_t *mag = (magnetometer_t *) mag_opaque;
    assert(mag != NULL);

    for (;;) {
        debugf(DEBUG, "Checking for magnetometer power command...");
        // wait for magnetometer power command
        while (!atomic_load_relaxed(mag->should_be_powered)) {
            debugf(DEBUG, "Waiting for magnetometer power command...");
            semaphore_take(&mag->flag_change);
        }
        debugf(DEBUG, "Turning on magnetometer power...");

        // turn on power
        if (!magnetometer_set_register(mag, REG_POWER, POWER_ON, NULL)) {
            debugf(CRITICAL, "Magnetometer: quitting read loop due to RMAP error.");
            break;
        }
        uint64_t powered_at = clock_timestamp_monotonic();
        tlm_mag_pwr_state_changed(&mag->telemetry_async, true);

        // take readings every 100ms until told to stop
        uint64_t reading_time = powered_at + READING_DELAY_NS;
        while (atomic_load_relaxed(mag->should_be_powered)) {
            // wait 100ms and check to confirm we weren't cancelled during that time
            if (semaphore_take_timed_abs(&mag->flag_change, reading_time)) {
                // need to recheck state... wake might be spurious.
                continue;
            }
            if (!atomic_load_relaxed(mag->should_be_powered)) {
                break;
            }

            // take and report reading
            tlm_mag_reading_t *reading = chart_request_start(&mag->readings);
            debugf(DEBUG, "Taking magnetometer reading...");
            if (!magnetometer_take_reading(mag, reading)) {
                debugf(CRITICAL, "Magnetometer: quitting read loop due to RMAP error.");
                return;
            }
            if (reading == NULL) {
                debugf(CRITICAL, "Magnetometer: out of space in queue to write readings.");
            } else {
                chart_request_send(&mag->readings, 1);
            }

            debugf(DEBUG, "Took magnetometer reading!");

            reading_time += READING_DELAY_NS;
        }

        // turn off power
        if (!magnetometer_set_register(mag, REG_POWER, POWER_OFF, NULL)) {
            debugf(CRITICAL, "Magnetometer: quitting read loop due to RMAP error.");
            break;
        }
        tlm_mag_pwr_state_changed(&mag->telemetry_async, false);
    }
}

static void magnetometer_telem_iterator_fetch(void *mag_opaque, size_t index, tlm_mag_reading_t *reading_out) {
    magnetometer_t *mag = (magnetometer_t *) mag_opaque;
    assert(mag != NULL && reading_out != NULL);

    *reading_out = *(tlm_mag_reading_t*) chart_reply_peek(&mag->readings, index);
}

static void magnetometer_telemloop(void *mag_opaque) {
    magnetometer_t *mag = (magnetometer_t *) mag_opaque;
    assert(mag != NULL);

    // runs every 5.5 seconds to meet requirements
    for (;;) {
        uint64_t last_telem_time = clock_timestamp_monotonic();

        // see if we have readings to downlink
        size_t downlink_count = chart_reply_avail(&mag->readings);
        if (downlink_count > 0) {
            size_t write_count = downlink_count;
            tlm_sync_mag_readings_map(&mag->telemetry_sync, &write_count, magnetometer_telem_iterator_fetch, mag);
            assert(write_count >= 1 && write_count <= downlink_count);
            chart_reply_send(&mag->readings, write_count);
        }

        sleep_until(last_telem_time + (uint64_t) 5500000000);
    }
}

static void magnetometer_drop_notification(void *opaque) {
    (void) opaque;
    // we're only using the chart as a datastructure, so no need for notifications.
}

void magnetometer_init(magnetometer_t *mag, rmap_addr_t *address, chart_t **rx_out, chart_t **tx_out) {
    assert(mag != NULL && address != NULL && rx_out != NULL && tx_out != NULL);
    chart_init(&mag->readings, sizeof(tlm_mag_reading_t), MAGNETOMETER_MAX_READINGS);
    chart_attach_server(&mag->readings, magnetometer_drop_notification, NULL);
    chart_attach_client(&mag->readings, magnetometer_drop_notification, NULL);
    tlm_async_init(&mag->telemetry_async);
    tlm_sync_init(&mag->telemetry_sync);
    semaphore_init(&mag->flag_change);
    mag->should_be_powered = false;
    rmap_init(&mag->endpoint, 8, 4, rx_out, tx_out);
    memcpy(&mag->address, address, sizeof(rmap_addr_t));
    thread_create(&mag->query_thread, "mag_query_loop", PRIORITY_WORKERS, magnetometer_mainloop, mag, RESTARTABLE);
    thread_create(&mag->telem_thread, "mag_telem_loop", PRIORITY_WORKERS, magnetometer_telemloop, mag, RESTARTABLE);
}

void magnetometer_set_powered(magnetometer_t *mag, bool powered) {
    assert(mag != NULL);
    if (powered != mag->should_be_powered) {
        atomic_store_relaxed(mag->should_be_powered, powered);
        semaphore_give(&mag->flag_change);
    }
}
