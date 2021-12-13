#ifndef FSW_CLOCK_INIT_H
#define FSW_CLOCK_INIT_H

#include <fsw/fakewire/rmap.h>
#include <fsw/fakewire/switch.h>

void clock_init(rmap_addr_t *address, chart_t **rx_out, chart_t **tx_out);
void clock_start(void);

#endif /* FSW_CLOCK_INIT_H */
