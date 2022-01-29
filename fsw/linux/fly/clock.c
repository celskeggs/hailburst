#include <assert.h>
#include <endian.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <hal/atomic.h>
#include <hal/clock.h>
#include <hal/clock_init.h>
#include <hal/debug.h>
#include <synch/retry.h>
#include <flight/telemetry.h>

int64_t clock_offset_adj = 0;

enum {
    CLOCK_MAGIC_NUM = 0x71CC70CC, /* tick-tock */

    REG_MAGIC  = 0x00,
    REG_CLOCK  = 0x04,
    REG_ERRORS = 0x0C,

    TRANSACTION_RETRIES = 100,

    CLOCK_RS_NOT_ALIGNED    = 1,
    CLOCK_RS_INVALID_ADDR   = 2,
    CLOCK_RS_INVALID_VALUE  = 3,
    CLOCK_RS_INVALID_LENGTH = 4,
    CLOCK_RS_CORRUPT_DATA   = 5,
};

static bool clock_calibrated = false;

static bool clock_read_register(clock_device_t *device, uint32_t reg, void *output, size_t len) {
    assert(device != NULL);
    rmap_status_t status;

    RETRY(TRANSACTION_RETRIES, "clock register %u read, error=0x%03x", reg, status) {
        status = rmap_read_exact(device->rmap, &device->address, RF_INCREMENT, 0x00, reg, len, output);
        if (status == RS_OK) {
            return true;
        }
    }
    return false;
}

void clock_wait_for_calibration(void) {
    assert(clock != NULL);
    while (!atomic_load(clock_calibrated)) {
        debugf(DEBUG, "Stuck waiting for clock calibration before telemetry can be timestamped.");
        local_doze(clock_cal_notify_task);
    }
}

TELEMETRY_ASYNC_REGISTER(clock_telemetry);

void clock_start_main(clock_device_t *clock) {
    assert(clock != NULL);
    assert(!clock_calibrated);

    // validate that this is actually a clock
    uint32_t magic_num = 0;
    if (!clock_read_register(clock, REG_MAGIC, &magic_num, sizeof(magic_num))) {
        abortf("Could not read magic number from clock.");
    }
    magic_num = be32toh(magic_num);
    assert(magic_num == CLOCK_MAGIC_NUM);

    uint64_t ref_time_sampled, local_time_postsampled;
    // sample once remotely and once locally
    if (!clock_read_register(clock, REG_CLOCK, &ref_time_sampled, sizeof(ref_time_sampled))) {
        abortf("Could not sample current time from clock.");
    }
    local_time_postsampled = clock_timestamp_monotonic();

    ref_time_sampled = be64toh(ref_time_sampled);

    debugf(INFO, "Timing details: ref=%"PRIu64" local=%"PRIu64, ref_time_sampled, local_time_postsampled);

    // now compute the appropriate offset
    clock_offset_adj = ref_time_sampled - local_time_postsampled;

    // notify anyone waiting
    atomic_store(clock_calibrated, true);
    local_rouse(clock_cal_notify_task);

    // and log our success, which will include a time using our new adjustment
    tlm_clock_calibrated(&clock_telemetry, clock_offset_adj);
}
