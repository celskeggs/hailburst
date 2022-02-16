#ifndef FSW_FAKEWIRE_SWITCH_H
#define FSW_FAKEWIRE_SWITCH_H

#include <stdint.h>
#include <stdbool.h>

#include <hal/init.h>
#include <hal/thread.h>
#include <synch/chart.h>

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

#define SWITCH_REGISTER(v_ident)                                                                                      \
    extern switch_t v_ident;                                                                                          \
    TASK_REGISTER(v_ident ## _task, switch_mainloop_internal, &v_ident, RESTARTABLE);                                 \
    switch_t v_ident = {                                                                                              \
        .ports_inbound = { NULL },                                                                                    \
        .ports_outbound = { NULL },                                                                                   \
        .routing_table = { 0 },                                                                                       \
        .switch_task = &v_ident ## _task,                                                                             \
    }

#define SWITCH_SCHEDULE(v_ident)                                                                                      \
    TASK_SCHEDULE(v_ident ## _task, 100)

// inbound is for packets TO the switch; the switch acts as the server.
// outbound is for packets FROM the switch; the switch acts as the client.
// these charts must pass io_rx_ent entries REGARDLESS OF DIRECTION!
#define SWITCH_PORT(v_ident, v_port, v_inbound, v_outbound)                                                           \
    CHART_SERVER_NOTIFY(v_inbound, task_rouse, v_ident.switch_task);                                                  \
    CHART_CLIENT_NOTIFY(v_outbound, task_rouse, v_ident.switch_task);                                                 \
    static_assert(SWITCH_PORT_BASE <= (v_port) && (v_port) < SWITCH_PORT_BASE + SWITCH_PORTS,                         \
                  "switch port must be valid");                                                                       \
    static void v_ident ## _port_ ## v_port ## _init(void) {                                                          \
        assert(io_rx_size(&v_inbound) > 0);                                                                           \
        assert(io_rx_size(&v_outbound) > 0);                                                                          \
        assert(v_ident.ports_inbound[(v_port) - SWITCH_PORT_BASE] == NULL);                                           \
        assert(v_ident.ports_outbound[(v_port) - SWITCH_PORT_BASE] == NULL);                                          \
        v_ident.ports_inbound[(v_port) - SWITCH_PORT_BASE] = &v_inbound;                                              \
        v_ident.ports_outbound[(v_port) - SWITCH_PORT_BASE] = &v_outbound;                                            \
    }                                                                                                                 \
    PROGRAM_INIT(STAGE_RAW, v_ident ## _port_ ## v_port ## _init)

#define SWITCH_ROUTE(v_ident, v_logical_address, v_port, v_address_pop)                                               \
    static_assert(SWITCH_ROUTE_BASE <= (v_logical_address), "switch route must be valid");                            \
    static_assert(SWITCH_PORT_BASE <= (v_port) && (v_port) < SWITCH_PORT_BASE + SWITCH_PORTS,                         \
                  "switch port must be valid");                                                                       \
    static void v_ident ## _route_ ## v_logical_address ## _init(void) {                                              \
        assert(v_ident.routing_table[(v_logical_address) - SWITCH_ROUTE_BASE] == 0);                                  \
        uint8_t route = (v_port) | SWITCH_ROUTE_FLAG_ENABLED;                                                         \
        if (v_address_pop) {                                                                                          \
            route |= SWITCH_ROUTE_FLAG_POP;                                                                           \
        }                                                                                                             \
        assert((route & SWITCH_ROUTE_PORT_MASK) == (v_port));                                                         \
        v_ident.routing_table[(v_logical_address) - SWITCH_ROUTE_BASE] = route;                                       \
    }                                                                                                                 \
    PROGRAM_INIT(STAGE_RAW, v_ident ## _route_ ## v_logical_address ## _init)

#endif /* FSW_FAKEWIRE_RMAP_H */
