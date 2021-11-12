#include <fsw/clock.h>
#include <fsw/clock_init.h>
#include <fsw/tlm.h>

// no adjustment needed on FreeRTOS.
void clock_init(rmap_monitor_t *mon, rmap_addr_t *address) {
    (void) mon;
    (void) address;

    tlm_clock_calibrated(0);
}
