#ifndef FSW_FAKEWIRE_SWITCH_H
#define FSW_FAKEWIRE_SWITCH_H

#include <stdint.h>
#include <stdbool.h>

#include <hal/thread.h>
#include <fsw/chart.h>
#include <fsw/init.h>

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

    thread_t switch_task;
} switch_t;

void switch_mainloop_internal(switch_t *sw);
void switch_init_internal(switch_t *sw);

#define SWITCH_REGISTER(v_ident)                                                                     \
    extern switch_t v_ident;                                                                         \
    TASK_REGISTER(v_ident ## _task, "switch_loop", switch_mainloop_internal, &v_ident, RESTARTABLE); \
    switch_t v_ident = {                                                                             \
        .ports_inbound = { NULL },                                                                   \
        .ports_outbound = { NULL },                                                                  \
        .routing_table = { 0 },                                                                      \
        .switch_task = &v_ident ## _task,                                                            \
    }

// inbound is for packets TO the switch; the switch acts as the server.
// outbound is for packets FROM the switch; the switch acts as the client.
// these charts must pass io_rx_ent entries REGARDLESS OF DIRECTION!
void switch_add_port(switch_t *sw, uint8_t port_number, chart_t *inbound, chart_t *outbound);
void switch_add_route(switch_t *sw, uint8_t logical_address, uint8_t port_number, bool address_pop);

#endif /* FSW_FAKEWIRE_RMAP_H */
