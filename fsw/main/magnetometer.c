#include <assert.h>
#include <endian.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>

#include <fsw/clock.h>
#include <fsw/debug.h>
#include <fsw/magnetometer.h>
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
};

static bool magnetometer_is_error_recoverable(rmap_status_t status) {
    assert(status != RS_OK);
    switch ((uint32_t) status) {
    // indicates likely packet corruption; worth retrying in case it works again.
    case RS_DATA_TRUNCATED:
        return true;
    case RS_TRANSACTION_TIMEOUT:
        return true;
    case MAG_RS_CORRUPT_DATA:
        return true;
    // indicates programming error or program code corruption; not worth retrying. we want these to be surfaced.
    case MAG_RS_NOT_ALIGNED:
        return false;
    case MAG_RS_INVALID_ADDR:
        return false;
    case MAG_RS_INVALID_VALUE:
        return false;
    // if not known, assume we can't recover.
    default:
        return false;
    }
}

static bool magnetometer_set_register(magnetometer_t *mag, uint32_t reg, uint16_t value) {
    rmap_status_t status;
    value = htobe16(value);
    int retries = TRANSACTION_RETRIES;

retry:
    status = rmap_write(&mag->rctx, &mag->address, RF_VERIFY | RF_ACKNOWLEDGE | RF_INCREMENT, 0x00, reg, 2, &value);
    if (status != RS_OK) {
        if (!magnetometer_is_error_recoverable(status)) {
            debugf("Magnetometer: encountered unrecoverable error while setting register %u: 0x%03x", reg, status);
            return false;
        } else if (retries > 0) {
            debugf("Magnetometer: retrying register %u set after recoverable error: 0x%03x", reg, status);
            goto retry;
        } else {
            debugf("Magnetometer: after %d retries, erroring out during register %u set: 0x%03x",
                   TRANSACTION_RETRIES, reg, status);
            return false;
        }
    }
    return true;
}

static void sleep_until(uint64_t target_time) {
    int64_t remain;
    while ((remain = target_time - clock_timestamp()) > 0) {
        usleep(remain / 1000);
    }
}

static bool magnetometer_take_reading(magnetometer_t *mag, tlm_mag_reading_t *reading_out) {
    // trigger reading
    if (!magnetometer_set_register(mag, REG_LATCH, LATCH_ON)) {
        return false;
    }
    reading_out->reading_time = rmap_get_ack_timestamp_ns(&mag->rctx);

    usleep(15000);

    rmap_status_t status;
    for (int loop_retries = 0; loop_retries < 50; loop_retries++) {
        _Static_assert(REG_LATCH + 1 == REG_X, "assumptions about register layout");
        _Static_assert(REG_LATCH + 2 == REG_Y, "assumptions about register layout");
        _Static_assert(REG_LATCH + 3 == REG_Z, "assumptions about register layout");

        uint16_t registers[4];
        size_t data_length;
        int retries = TRANSACTION_RETRIES;

retry:
        data_length = sizeof(registers);
        status = rmap_read(&mag->rctx, &mag->address, RF_INCREMENT, 0x00, REG_LATCH, &data_length, registers);

        if (status != RS_OK) {
            if (!magnetometer_is_error_recoverable(status)) {
                debugf("Magnetometer: encountered unrecoverable error while reading registers: 0x%03x", status);
                return false;
            } else if (retries > 0) {
                debugf("Magnetometer: retrying register read after recoverable error: 0x%03x", status);
                goto retry;
            } else {
                debugf("Magnetometer: after %d retries, erroring out during register read: 0x%03x",
                       TRANSACTION_RETRIES, status);
                return false;
            }
        }
        if (data_length != sizeof(registers)) {
            debugf("Magnetometer: invalid length while reading registers: %zu instead of %zu",
                   data_length, sizeof(registers));
            return false;
        }

        for (int i = 0; i < 4; i++) {
            registers[i] = be16toh(registers[i]);
        }

        assert(registers[0] == LATCH_OFF || registers[0] == LATCH_ON);
        if (registers[0] == LATCH_OFF) {
            reading_out->mag_x = registers[REG_X - REG_LATCH];
            reading_out->mag_y = registers[REG_Y - REG_LATCH];
            reading_out->mag_z = registers[REG_Z - REG_LATCH];
            return true;
        }

        usleep(200);
    }
    debug0("Magnetometer: ran out of loop retries while trying to take a reading.");
    return false;
}

static void *magnetometer_mainloop(void *mag_opaque) {
    magnetometer_t *mag = (magnetometer_t *) mag_opaque;
    assert(mag != NULL);

    for (;;) {
        debug0("Checking for magnetometer power command...");
        mutex_lock(&mag->mutex);
        // wait for magnetometer power command
        while (!mag->should_be_powered) {
            mutex_unlock(&mag->mutex);
            debug0("Waiting for magnetometer power command...");
            semaphore_take(&mag->flag_change);
            mutex_lock(&mag->mutex);
        }
        assert(mag->should_be_powered == true);
        mutex_unlock(&mag->mutex);
        debug0("Turning on magnetometer power...");

        // turn on power
        if (!magnetometer_set_register(mag, REG_POWER, POWER_ON)) {
            debug0("Magnetometer: quitting read loop due to RMAP error.");
            return NULL;
        }
        uint64_t powered_at = clock_timestamp();
        tlm_mag_pwr_state_changed(true);

        // take readings every 100ms until told to stop
        uint64_t reading_time = powered_at;
        mutex_lock(&mag->mutex);
        while (mag->should_be_powered) {
            // wait 100ms and check to confirm we weren't cancelled during that time
            reading_time += 100 * 1000 * 1000;
            mutex_unlock(&mag->mutex);
            sleep_until(reading_time);
            mutex_lock(&mag->mutex);
            if (!mag->should_be_powered) {
                break;
            }
            mutex_unlock(&mag->mutex);

            // take and report reading
            tlm_mag_reading_t reading;
            if (!magnetometer_take_reading(mag, &reading)) {
                debug0("Magnetometer: quitting read loop due to RMAP error.");
                return NULL;
            }

            mutex_lock(&mag->mutex);
            if (mag->num_readings < MAGNETOMETER_MAX_READINGS) {
                mag->readings[mag->num_readings++] = reading;
            } else {
                debugf("Magnetometer: maxed out at %zu collected readings.", mag->num_readings);
            }
        }
        mutex_unlock(&mag->mutex);

        // turn off power
        if (!magnetometer_set_register(mag, REG_POWER, POWER_OFF)) {
            debug0("Magnetometer: quitting read loop due to RMAP error.");
            return NULL;
        }
        tlm_mag_pwr_state_changed(false);
    }
}

static void *magnetometer_telemloop(void *mag_opaque) {
    magnetometer_t *mag = (magnetometer_t *) mag_opaque;
    assert(mag != NULL);

    // runs every 5.5 seconds to meet requirements
    for (;;) {
        uint64_t last_telem_time = clock_timestamp();

        // see if we have readings to downlink
        mutex_lock(&mag->mutex);
        if (mag->num_readings > 0) {
            // snapshot readings to send
            size_t num_downlink = mag->num_readings;
            assert(num_downlink <= MAGNETOMETER_MAX_READINGS);
            mutex_unlock(&mag->mutex);

            // send readings
            tlm_sync_mag_readings_array(mag->readings, num_downlink);

            last_telem_time = clock_timestamp();

            // rearrange any readings collected in the interim
            mutex_lock(&mag->mutex);
            mag->num_readings -= num_downlink;
            assert(mag->num_readings <= MAGNETOMETER_MAX_READINGS);
            memmove(&mag->readings[0], &mag->readings[num_downlink], mag->num_readings);
        }
        mutex_unlock(&mag->mutex);

        sleep_until(last_telem_time + (uint64_t) 5500000000);
    }
}

void magnetometer_init(magnetometer_t *mag, rmap_monitor_t *mon, rmap_addr_t *address) {
    assert(mag != NULL && mon != NULL && address != NULL);
    mutex_init(&mag->mutex);
    semaphore_init(&mag->flag_change);
    mag->should_be_powered = false;
    rmap_init_context(&mag->rctx, mon, 4);
    memcpy(&mag->address, address, sizeof(rmap_addr_t));
    thread_create(&mag->query_thread, "mag_query_loop", PRIORITY_WORKERS, magnetometer_mainloop, mag);
    thread_create(&mag->telem_thread, "mag_telem_loop", PRIORITY_WORKERS, magnetometer_telemloop, mag);
}

void magnetometer_set_powered(magnetometer_t *mag, bool powered) {
    assert(mag != NULL);
    mutex_lock(&mag->mutex);
    if (powered != mag->should_be_powered) {
        mag->should_be_powered = powered;
        debugf("Notifying loop that should_be_powered=%d", mag->should_be_powered);
        semaphore_give(&mag->flag_change);
    }
    mutex_unlock(&mag->mutex);
}
