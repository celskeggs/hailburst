#include <assert.h>
#include <endian.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <fsw/clock.h>
#include <fsw/clock_init.h>
#include <fsw/debug.h>
#include <fsw/retry.h>
#include <fsw/telemetry.h>

int64_t clock_offset_adj = 0;

typedef struct {
    bool initialized;
    bool calibrated;

    semaphore_t wake_calibrated;

    rmap_t       rmap;
    rmap_addr_t *address;

    tlm_async_endpoint_t telemetry;
} clock_device_t;

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

static clock_device_t clock_device;

static bool clock_read_register(uint32_t reg, void *output, size_t len) {
    assert(clock_device.initialized == true);
    rmap_status_t status;

    RETRY(TRANSACTION_RETRIES, "clock register %u read, error=0x%03x", reg, status) {
        status = rmap_read_exact(&clock_device.rmap, clock_device.address, RF_INCREMENT, 0x00, reg, len, output);
        if (status == RS_OK) {
            return true;
        }
    }
    return false;
}

void clock_init(rmap_addr_t *address, chart_t **rx_out, chart_t **tx_out) {
    assert(address != NULL && rx_out != NULL && tx_out != NULL);
    assert(!clock_device.initialized);

    semaphore_init(&clock_device.wake_calibrated);

    clock_device.initialized = true;

    tlm_async_init(&clock_device.telemetry);

    rmap_init(&clock_device.rmap, sizeof(uint64_t), 0, rx_out, tx_out);
    clock_device.address = address;
}

void clock_wait_for_calibration(void) {
    assert(clock_device.initialized);
    while (!clock_device.calibrated) {
        debugf(DEBUG, "Stuck waiting for clock calibration before telemetry can be timestamped.");
        semaphore_take(&clock_device.wake_calibrated);
        // wake up anyone else waiting
        (void) semaphore_give(&clock_device.wake_calibrated);
    }
}

static void clock_start_main(void *opaque) {
    (void) opaque;
    assert(clock_device.initialized);

    // validate that this is actually a clock
    uint32_t magic_num = 0;
    if (!clock_read_register(REG_MAGIC, &magic_num, sizeof(magic_num))) {
        abortf("Could not read magic number from clock.");
    }
    magic_num = be32toh(magic_num);
    assert(magic_num == CLOCK_MAGIC_NUM);

    uint64_t ref_time_sampled, local_time_postsampled;
    // sample once remotely and once locally
    if (!clock_read_register(REG_CLOCK, &ref_time_sampled, sizeof(ref_time_sampled))) {
        abortf("Could not sample current time from clock.");
    }
    local_time_postsampled = clock_timestamp_monotonic();

    ref_time_sampled = be64toh(ref_time_sampled);

    debugf(INFO, "Timing details: ref=%"PRIu64" local=%"PRIu64, ref_time_sampled, local_time_postsampled);

    // now compute the appropriate offset
    clock_offset_adj = ref_time_sampled - local_time_postsampled;

    // notify anyone waiting
    clock_device.calibrated = true;
    semaphore_give(&clock_device.wake_calibrated);

    // and log our success, which will include a time using our new adjustment
    tlm_clock_calibrated(&clock_device.telemetry, clock_offset_adj);
}
TASK_REGISTER(clock_start_task, "clock-start", PRIORITY_INIT, clock_start_main, NULL, NOT_RESTARTABLE);
