#include <assert.h>
#include <endian.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include <fsw/clock.h>
#include <fsw/debug.h>
#include <fsw/tlm.h>

int64_t clock_offset_adj = 0;

typedef struct {
    bool initialized;
    rmap_context_t context;
    rmap_addr_t *address;
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

static bool clock_is_error_recoverable(rmap_status_t status) {
    assert(status != RS_OK);
    switch ((uint32_t) status) {
    // indicates likely packet corruption; worth retrying in case it works again.
    case RS_DATA_TRUNCATED:
        return true;
    case RS_TRANSACTION_TIMEOUT:
        return true;
    case CLOCK_RS_CORRUPT_DATA:
        return true;
    // indicates link loss; worth retrying in case it gets re-established.
    case RS_TRANSMIT_TIMEOUT:
        return true;
    case RS_TRANSMIT_BLOCKED:
        return true;
    // indicates programming error or program code corruption; not worth retrying. we want these to be surfaced.
    case CLOCK_RS_NOT_ALIGNED:
        return false;
    case CLOCK_RS_INVALID_ADDR:
        return false;
    case CLOCK_RS_INVALID_VALUE:
        return false;
    case CLOCK_RS_INVALID_LENGTH:
        return false;
    // if not known, assume we can't recover.
    default:
        return false;
    }
}

static bool clock_read_register(uint32_t reg, void *output, size_t len) {
    assert(clock_device.initialized == true);
    rmap_status_t status;
    size_t read_len;
    int retries = TRANSACTION_RETRIES;

retry:
    read_len = len;
    status = rmap_read(&clock_device.context, clock_device.address, RF_INCREMENT, 0x00, reg, &read_len, output);
    if (status != RS_OK) {
        if (!clock_is_error_recoverable(status)) {
            debugf("Clock: encountered unrecoverable error while reading register %u: 0x%03x", reg, status);
            return false;
        } else if (retries > 0) {
            debugf("Clock: retrying register %u read after recoverable error: 0x%03x", reg, status);
            retries -= 1;
            goto retry;
        } else {
            debugf("Clock: after %d retries, erroring out during register %u read: 0x%03x",
                   TRANSACTION_RETRIES, reg, status);
            return false;
        }
    }
    assert(status == RS_OK);
    assert(read_len == len);
    return true;
}

void clock_init(rmap_monitor_t *mon, rmap_addr_t *address) {
    assert(mon != NULL && address != NULL);
    assert(!clock_device.initialized);
    clock_device.initialized = true;

    rmap_init_context(&clock_device.context, mon, 0);
    clock_device.address = address;

    // validate that this is actually a clock
    uint32_t magic_num = 0;
    if (!clock_read_register(REG_MAGIC, &magic_num, sizeof(magic_num))) {
        debugf("Could not read magic number from clock.");
        abort();
    }
    magic_num = be32toh(magic_num);
    assert(magic_num == CLOCK_MAGIC_NUM);

    uint64_t ref_time_sampled, local_time_postsampled;
    // sample once remotely and once locally
    if (!clock_read_register(REG_CLOCK, &ref_time_sampled, sizeof(ref_time_sampled))) {
        debugf("Could not sample current time from clock.");
        abort();
    }
    local_time_postsampled = clock_timestamp_monotonic();

    ref_time_sampled = be64toh(ref_time_sampled);

    debugf("Timing details: ref=%"PRIu64" local=%"PRIu64, ref_time_sampled, local_time_postsampled);

    // now compute the appropriate offset
    clock_offset_adj = ref_time_sampled - local_time_postsampled;

    // and log our success, which will include a time using our new adjustment
    tlm_clock_calibrated(clock_offset_adj);
}
