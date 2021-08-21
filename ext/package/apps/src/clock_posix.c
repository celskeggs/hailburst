#include <arpa/inet.h>
#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include "clock.h"
#include "debug.h"
#include "tlm.h"

int64_t clock_offset_adj = 0;

enum {
	CLOCK_MAGIC_NUM = 0x71CC70CC, /* tick-tock */

    REG_MAGIC  = 0x00,
    REG_CLOCK  = 0x04,
    REG_ERRORS = 0x0C,
};

static bool clock_initialized = false;
static rmap_context_t clock_ctx;

void clock_init(rmap_monitor_t *mon, rmap_addr_t *address) {
    assert(mon != NULL && address != NULL);
    assert(!clock_initialized);
    clock_initialized = true;

    rmap_init_context(&clock_ctx, mon, 0);

    // validate that this is actually a clock
    uint32_t magic_num;
    size_t read_len = sizeof(magic_num);
    rmap_status_t status;
    status = rmap_read(&clock_ctx, address, RF_INCREMENT, 0x00, REG_MAGIC, &read_len, &magic_num);
    assert(status == RS_OK);
    assert(read_len == sizeof(magic_num));
    magic_num = ntohl(magic_num);
    assert(magic_num == CLOCK_MAGIC_NUM);

    // prepare for a fast set of samples
    uint32_t ref_time_sampled_raw[2];
    read_len = sizeof(ref_time_sampled_raw);
    uint64_t local_time_postsampled;

    // sample twice locally and once remotely
    status = rmap_read(&clock_ctx, address, RF_INCREMENT, 0x00, REG_CLOCK, &read_len, &ref_time_sampled_raw[0]);
    local_time_postsampled = clock_timestamp_monotonic();

    // make sure we communicated correctly and decode bytes
    assert(status == RS_OK);
    assert(read_len == sizeof(ref_time_sampled_raw));
    uint64_t ref_time_sampled = ((uint64_t) ntohl(ref_time_sampled_raw[0]) << 32) | ntohl(ref_time_sampled_raw[1]);

    // debugf("Timing details: local_pre=%"PRIu64" ref=%"PRIu64" local_post=%"PRIu64, local_time_presampled, ref_time_sampled, local_time_postsampled);

    // now compute the appropriate offset
    clock_offset_adj = ref_time_sampled - local_time_postsampled;

    // and log our success, which will include a time using our new adjustment
    tlm_clock_calibrated(clock_offset_adj);
}
