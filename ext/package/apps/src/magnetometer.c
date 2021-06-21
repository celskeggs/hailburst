#include <arpa/inet.h>
#include <assert.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>

#include "clock.h"
#include "debug.h"
#include "magnetometer.h"
#include "tlm.h"

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
};

static void magnetometer_set_register(magnetometer_t *mag, uint32_t reg, uint16_t value) {
    rmap_status_t status;
    value = htons(value);
    status = rmap_write(&mag->rctx, &mag->address, RF_VERIFY | RF_ACKNOWLEDGE | RF_INCREMENT,
                        0x00, reg, 2, &value);
    assert(status == RS_OK);
}

static void sleep_until(uint64_t target_time) {
    int64_t remain;
    while ((remain = target_time - clock_timestamp()) > 0) {
        usleep(remain / 1000);
    }
}

static void magnetometer_take_reading(magnetometer_t *mag, tlm_mag_reading_t *reading_out) {
    // trigger reading
    debug0("Setting registers...");
    magnetometer_set_register(mag, REG_LATCH, LATCH_ON);
    reading_out->reading_time = clock_timestamp();
    debugf("Stopped setting registers; timestamp was %"PRIu64, reading_out->reading_time);

    usleep(15000);

    rmap_status_t status;
    for (;;) {
        _Static_assert(REG_LATCH + 1 == REG_X, "assumptions about register layout");
        _Static_assert(REG_LATCH + 2 == REG_Y, "assumptions about register layout");
        _Static_assert(REG_LATCH + 3 == REG_Z, "assumptions about register layout");

        uint16_t registers[4];
        size_t data_length = sizeof(registers);
        status = rmap_read(&mag->rctx, &mag->address, RF_INCREMENT, 0x00, REG_LATCH, &data_length, registers);
        assert(status == RS_OK && data_length == sizeof(registers));
        for (int i = 0; i < 4; i++) {
            registers[i] = ntohs(registers[i]);
        }

        assert(registers[0] == LATCH_OFF || registers[0] == LATCH_ON);
        if (registers[0] == LATCH_OFF) {
            reading_out->mag_x = registers[REG_X - REG_LATCH];
            reading_out->mag_y = registers[REG_Y - REG_LATCH];
            reading_out->mag_z = registers[REG_Z - REG_LATCH];
            break;
        }

        usleep(200);
    }
}

static void *magnetometer_mainloop(void *mag_opaque) {
    magnetometer_t *mag = (magnetometer_t *) mag_opaque;
    assert(mag != NULL);

    for (;;) {
        debug0("Checking for magnetometer power command...");
        mutex_lock(&mag->mutex);
        // wait for magnetometer power command
        while (!mag->should_be_powered) {
            debug0("Waiting for magnetometer power command...");
            cond_wait(&mag->cond, &mag->mutex);
        }
        assert(mag->should_be_powered == true);
        mutex_unlock(&mag->mutex);
        debug0("Turning on magnetometer power...");

        // turn on power
        magnetometer_set_register(mag, REG_POWER, POWER_ON);
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
            magnetometer_take_reading(mag, &reading);

            mutex_lock(&mag->mutex);
            if (mag->num_readings < MAGNETOMETER_MAX_READINGS) {
                mag->readings[mag->num_readings++] = reading;
            } else {
                debugf("Magnetometer: maxed out at %zu collected readings.", mag->num_readings);
            }
        }
        mutex_unlock(&mag->mutex);

        // turn off power
        magnetometer_set_register(mag, REG_POWER, POWER_OFF);
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
    cond_init(&mag->cond);
    mag->should_be_powered = false;
    rmap_init_context(&mag->rctx, mon, 4);
    memcpy(&mag->address, address, sizeof(rmap_addr_t));
    thread_create(&mag->query_thread, magnetometer_mainloop, mag);
    thread_create(&mag->telem_thread, magnetometer_telemloop, mag);
}

void magnetometer_set_powered(magnetometer_t *mag, bool powered) {
    assert(mag != NULL);
    mutex_lock(&mag->mutex);
    if (powered != mag->should_be_powered) {
        mag->should_be_powered = powered;
        debugf("Broadcasting &mag->cond with should_be_powered=%d", mag->should_be_powered);
        cond_broadcast(&mag->cond);
    }
    mutex_unlock(&mag->mutex);
}
