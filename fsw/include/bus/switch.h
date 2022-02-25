#ifndef FSW_FAKEWIRE_SWITCH_H
#define FSW_FAKEWIRE_SWITCH_H

#include <stdint.h>
#include <stdbool.h>

#include <hal/init.h>
#include <hal/thread.h>
#include <synch/duct.h>

#define SWITCH_REPLICAS 3

enum {
    SWITCH_PORT_BASE  = 1,
    SWITCH_PORTS      = 31,
    SWITCH_ROUTE_BASE = 32,
    SWITCH_ROUTES     = 256 - 32,

    SWITCH_ROUTE_PORT_MASK    = 0x1F,
    SWITCH_ROUTE_FLAG_ENABLED = 0x40,
    SWITCH_ROUTE_FLAG_POP     = 0x80,
};

// TODO: figure out how to prebuild this structure so that it can be const
typedef struct {
    duct_t *ports_inbound[SWITCH_PORTS];
    duct_t *ports_outbound[SWITCH_PORTS];

    size_t scratch_buffer_size;

    uint8_t routing_table[SWITCH_ROUTES];
} switch_t;

typedef struct {
    switch_t *replica_switch;
    uint8_t  *scratch_buffer;
    uint8_t   replica_id;
} switch_replica_t;

void switch_mainloop_internal(const switch_replica_t *sr);

#define SWITCH_REGISTER(v_ident, v_max_buffer)                                                                        \
    switch_t v_ident = {                                                                                              \
        .ports_inbound = { NULL },                                                                                    \
        .ports_outbound = { NULL },                                                                                   \
        .scratch_buffer_size = (v_max_buffer),                                                                        \
        .routing_table = { 0 },                                                                                       \
    };                                                                                                                \
    static_repeat(SWITCH_REPLICAS, switch_replica_id) {                                                               \
        uint8_t symbol_join(v_ident, scratch_buffer, switch_replica_id)[v_max_buffer];                                \
        const switch_replica_t symbol_join(v_ident, replica, switch_replica_id) = {                                   \
            .replica_switch = &v_ident,                                                                               \
            .scratch_buffer = symbol_join(v_ident, scratch_buffer, switch_replica_id),                                \
            .replica_id     = switch_replica_id,                                                                      \
        };                                                                                                            \
        TASK_REGISTER(symbol_join(v_ident, task, switch_replica_id), switch_mainloop_internal,                        \
                      &symbol_join(v_ident, replica, switch_replica_id), RESTARTABLE);                                \
    }

#define SWITCH_SCHEDULE(v_ident)                                                                                      \
    static_repeat(SWITCH_REPLICAS, switch_replica_id) {                                                               \
        TASK_SCHEDULE(symbol_join(v_ident, task, switch_replica_id), 100)                                             \
    }

// inbound is for packets TO the switch.
#define SWITCH_PORT_INBOUND(v_ident, v_port, v_inbound)                                                               \
    static_assert(SWITCH_PORT_BASE <= (v_port) && (v_port) < SWITCH_PORT_BASE + SWITCH_PORTS,                         \
                  "switch port must be valid");                                                                       \
    static void v_ident ## _port_ ## v_port ## _init_inbound(void) {                                                  \
        assert(duct_message_size(&(v_inbound)) <= v_ident.scratch_buffer_size);                                       \
        assert(v_ident.ports_inbound[(v_port) - SWITCH_PORT_BASE] == NULL);                                           \
        v_ident.ports_inbound[(v_port) - SWITCH_PORT_BASE] = &v_inbound;                                              \
    }                                                                                                                 \
    PROGRAM_INIT(STAGE_RAW, v_ident ## _port_ ## v_port ## _init_inbound)

// outbound is for packets FROM the switch.
#define SWITCH_PORT_OUTBOUND(v_ident, v_port, v_outbound)                                                             \
    static_assert(SWITCH_PORT_BASE <= (v_port) && (v_port) < SWITCH_PORT_BASE + SWITCH_PORTS,                         \
                  "switch port must be valid");                                                                       \
    static void v_ident ## _port_ ## v_port ## _init_outbound(void) {                                                 \
        /* no need to check outbound message size; we can detect if a truncation is necessary! */                     \
        assert(v_ident.ports_outbound[(v_port) - SWITCH_PORT_BASE] == NULL);                                          \
        v_ident.ports_outbound[(v_port) - SWITCH_PORT_BASE] = &v_outbound;                                            \
    }                                                                                                                 \
    PROGRAM_INIT(STAGE_RAW, v_ident ## _port_ ## v_port ## _init_outbound)

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
