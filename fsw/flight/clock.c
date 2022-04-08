#include <endian.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

#include <hal/atomic.h>
#include <hal/debug.h>
#include <flight/clock.h>
#include <flight/clock_cal.h>
#include <flight/telemetry.h>

int64_t clock_offset_adj_slow[CLOCK_REPLICAS] = { [0 ... CLOCK_REPLICAS-1] = CLOCK_UNCALIBRATED, };
int64_t clock_offset_adj_fast = CLOCK_UNCALIBRATED;
bool clock_calibration_required = true;

enum {
    CLOCK_MAGIC_NUM = 0x71CC70CC, /* tick-tock */

    REG_MAGIC  = 0x00,
    REG_CLOCK  = 0x04,
    REG_ERRORS = 0x0C,
};

static void clock_configure(tlm_txn_t *telem, uint8_t replica_id,
                            mission_time_t received_timestamp, local_time_t network_timestamp) {
    debugf(INFO, "[%u] Timing details: ref=%"PRIu64" local=%"PRIu64,
           replica_id, received_timestamp, network_timestamp);

    // now compute the appropriate offset
    int64_t adjustment = received_timestamp - network_timestamp;
    if (adjustment == CLOCK_UNCALIBRATED) {
        // make sure that 0 is reserved for the actual state of not being calibrated; a 1ns discrepancy is fine.
        adjustment++;
    }
    clock_offset_adj_slow[replica_id] = adjustment;

    // and log our success, which will include a time using our new adjustment
    tlm_clock_calibrated(telem, adjustment);
}

void clock_voter_clip(void) {
    clock_offset_adj_fast = clock_offset_adj_vote();
    size_t mismatches = 0;
    for (size_t i = 0; i < CLOCK_REPLICAS; i++) {
        if (clock_offset_adj_fast != clock_offset_adj_slow[i]) {
            mismatches++;
        }
    }
    bool calibration_required_local = (clock_offset_adj_fast == CLOCK_UNCALIBRATED || mismatches > 0);
    if (calibration_required_local != clock_calibration_required) {
        debugf(DEBUG, "Setting clock_calibration_required = %u (offset_fast=" TIMEFMT ", mismatches=%zu)",
               calibration_required_local, TIMEARG(clock_offset_adj_fast), mismatches);
        atomic_store(clock_calibration_required, calibration_required_local);
    }
}

void clock_start_clip(clock_replica_t *cr) {
    assert(cr != NULL && cr->mut != NULL);

    // temporary local variables for switch statements
    rmap_status_t status;
    uint32_t magic_number;
    mission_time_t received_timestamp;
    local_time_t network_timestamp;

    rmap_txn_t rmap_txn;
    rmap_epoch_prepare(&rmap_txn, cr->rmap);
    tlm_txn_t telem_txn;
    telemetry_prepare(&telem_txn, cr->telem, cr->replica_id);

    switch (cr->mut->state) {
    case CLOCK_READ_MAGIC_NUMBER:
        status = rmap_read_complete(&rmap_txn, (uint8_t*) &magic_number, sizeof(magic_number), NULL);
        if (status == RS_OK) {
            magic_number = be32toh(magic_number);
            if (magic_number != CLOCK_MAGIC_NUM) {
                abortf("Clock sent incorrect magic number.");
            }
            cr->mut->state = CLOCK_READ_CURRENT_TIME;
        } else {
            debugf(WARNING, "Failed to query clock magic number, error=0x%03x", status);
        }
        break;
    case CLOCK_READ_CURRENT_TIME:
        status = rmap_read_complete(&rmap_txn, (uint8_t*) &received_timestamp, sizeof(received_timestamp),
                                    &network_timestamp);
        if (status == RS_OK) {
            received_timestamp = be64toh(received_timestamp);

            clock_configure(&telem_txn, cr->replica_id, received_timestamp, network_timestamp);

            cr->mut->state = CLOCK_CALIBRATED;
        } else {
            debugf(WARNING, "Failed to query clock current time, error=0x%03x", status);
        }
        break;
    default:
        /* nothing to do */
        break;
    }

    if (cr->mut->state == CLOCK_IDLE && atomic_load(clock_calibration_required)) {
        cr->mut->state = CLOCK_READ_MAGIC_NUMBER;
    } else if (cr->mut->state == CLOCK_CALIBRATED) {
        cr->mut->state = CLOCK_IDLE;
    }

    switch (cr->mut->state) {
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
