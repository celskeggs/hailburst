#ifndef FSW_FAKEWIRE_SWITCH_H
#define FSW_FAKEWIRE_SWITCH_H

#include <stdint.h>
#include <stdbool.h>

#include <hal/thread.h>
#include <fsw/chart.h>

enum {
    SWITCH_PORT_BASE  = 1,
    SWITCH_PORTS      = 31,
    SWITCH_ROUTE_BASE = 32,
    SWITCH_ROUTES     = 256 - 32,

    SWITCH_ROUTE_PORT_MASK    = 0x1F,
    SWITCH_ROUTE_FLAG_ENABLED = 0x40,
    SWITCH_ROUTE_FLAG_POP     = 0x80,
};

typedef struct {
    chart_t *ports_inbound[SWITCH_PORTS];
    chart_t *ports_outbound[SWITCH_PORTS];

    uint8_t routing_table[SWITCH_ROUTES];

    semaphore_t switching_wake;
    thread_t    switching_loop;
} switch_t;

void switch_init(switch_t *sw);
// inbound is for packets TO the switch; the switch acts as the server.
// outbound is for packets FROM the switch; the switch acts as the client.
// these charts must pass io_rx_ent entries REGARDLESS OF DIRECTION!
void switch_add_port(switch_t *sw, uint8_t port_number, chart_t *inbound, chart_t *outbound);
void switch_add_route(switch_t *sw, uint8_t logical_address, uint8_t port_number, bool address_pop);

#endif /* FSW_FAKEWIRE_RMAP_H */
