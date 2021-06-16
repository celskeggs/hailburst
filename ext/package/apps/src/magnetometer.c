#include <assert.h>
#include <string.h>
#include <unistd.h>

#include "clock.h"
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
    magnetometer_set_register(mag, REG_LATCH, LATCH_ON);
    reading_out->reading_time = clock_timestamp();

    rmap_status_t status;
    for (;;) {
        _Static_assert(REG_LATCH + 1 == REG_X, "assumptions about register layout");
        _Static_assert(REG_LATCH + 2 == REG_Y, "assumptions about register layout");
        _Static_assert(REG_LATCH + 3 == REG_Z, "assumptions about register layout");

        uint16_t registers[4];
        size_t data_length = sizeof(registers);
        status = rmap_read(&mag->rctx, &mag->address, RF_INCREMENT, 0x00, REG_LATCH, &data_length, registers);
        assert(status == RS_OK);

        assert(registers[0] == LATCH_OFF || registers[0] == LATCH_ON);
        if (registers[0] == LATCH_OFF) {
            reading_out->mag_x = registers[REG_X - REG_LATCH];
            reading_out->mag_y = registers[REG_Y - REG_LATCH];
            reading_out->mag_z = registers[REG_Z - REG_LATCH];
            break;
        }

        // wait 5.1 ms and check again
        usleep(5100);
    }
}

static void *magnetometer_mainloop(void *mag_opaque) {
    magnetometer_t *mag = (magnetometer_t *) mag_opaque;
    assert(mag != NULL);

    for (;;) {
        mutex_lock(&mag->mutex);
        // wait for magnetometer power command
        while (!mag->should_be_powered) {
            cond_wait(&mag->cond, &mag->mutex);
        }
        assert(mag->should_be_powered == true);
        mutex_unlock(&mag->mutex);

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
            tlm_mag_readings_array(&reading, 1);

            mutex_lock(&mag->mutex);
        }
        mutex_unlock(&mag->mutex);

        // turn off power
        magnetometer_set_register(mag, REG_POWER, POWER_OFF);
        tlm_mag_pwr_state_changed(false);
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
}

void magnetometer_set_powered(magnetometer_t *mag, bool powered) {
    assert(mag != NULL);
    mutex_lock(&mag->mutex);
    if (powered != mag->should_be_powered) {
        mag->should_be_powered = powered;
        cond_broadcast(&mag->cond);
    }
    mutex_unlock(&mag->mutex);
}
