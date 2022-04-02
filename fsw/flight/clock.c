#include <endian.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

#include <hal/atomic.h>
#include <hal/debug.h>
#include <synch/retry.h>
#include <flight/clock.h>
#include <flight/clock_cal.h>
#include <flight/telemetry.h>

bool clock_calibrated = false;
int64_t clock_offset_adj = 0;

enum {
    CLOCK_MAGIC_NUM = 0x71CC70CC, /* tick-tock */

    CLOCK_REPLICA_ID = 0,

    REG_MAGIC  = 0x00,
    REG_CLOCK  = 0x04,
    REG_ERRORS = 0x0C,
};

bool clock_is_calibrated(void) {
    return atomic_load(clock_calibrated);
}

static void clock_configure(tlm_txn_t *telem, mission_time_t received_timestamp, local_time_t network_timestamp) {
    assert(!clock_calibrated);

    debugf(INFO, "Timing details: ref=%"PRIu64" local=%"PRIu64, received_timestamp, network_timestamp);

    // now compute the appropriate offset
    clock_offset_adj = received_timestamp - network_timestamp;

    // notify anyone waiting
    atomic_store(clock_calibrated, true);

    // and log our success, which will include a time using our new adjustment
    tlm_clock_calibrated(telem, clock_offset_adj);
}

void clock_start_clip(clock_device_t *clock) {
    assert(clock != NULL);

    // temporary local variables for switch statements
    rmap_status_t status;
    uint32_t magic_number;
    mission_time_t received_timestamp;
    local_time_t network_timestamp;

    // this clip only does one thing during init, and then sits there doing nothing for the rest of the time.
    // TODO: can we reclaim this resource?

    rmap_txn_t rmap_txn;
    rmap_epoch_prepare(&rmap_txn, clock->rmap);
    tlm_txn_t telem_txn;
    telemetry_prepare(&telem_txn, clock->telem, CLOCK_REPLICA_ID);

    switch (clock->state) {
    case CLOCK_READ_MAGIC_NUMBER:
        status = rmap_read_complete(&rmap_txn, (uint8_t*) &magic_number, sizeof(magic_number), NULL);
        if (status == RS_OK) {
            magic_number = be32toh(magic_number);
            if (magic_number != CLOCK_MAGIC_NUM) {
                abortf("Clock sent incorrect magic number.");
            }
            clock->state = CLOCK_READ_CURRENT_TIME;
        } else {
            debugf(WARNING, "Failed to query clock magic number, error=0x%03x", status);
        }
        break;
    case CLOCK_READ_CURRENT_TIME:
        status = rmap_read_complete(&rmap_txn, (uint8_t*) &received_timestamp, sizeof(received_timestamp),
                                    &network_timestamp);
        if (status == RS_OK) {
            received_timestamp = be64toh(received_timestamp);

            clock_configure(&telem_txn, received_timestamp, network_timestamp);

            clock->state = CLOCK_IDLE;
        } else {
            debugf(WARNING, "Failed to query clock current time, error=0x%03x", status);
        }
        break;
    default:
        /* nothing to do */
        break;
    }

    if (clock->state == CLOCK_INITIAL_STATE) {
        clock->state = CLOCK_READ_MAGIC_NUMBER;
    }

    switch (clock->state) {
    case CLOCK_READ_MAGIC_NUMBER:
        rmap_read_start(&rmap_txn, 0x00, REG_MAGIC, sizeof(magic_number));
        break;
    case CLOCK_READ_CURRENT_TIME:
        rmap_read_start(&rmap_txn, 0x00, REG_CLOCK, sizeof(received_timestamp));
        break;
    default:
        /* nothing to do */
        break;
    }

    telemetry_commit(&telem_txn);
    rmap_epoch_commit(&rmap_txn);
}
