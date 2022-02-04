#ifndef FSW_FAKEWIRE_SWITCH_H
#define FSW_FAKEWIRE_SWITCH_H

#include <stdint.h>
#include <stdbool.h>

#include <hal/init.h>
#include <hal/thread.h>
#include <synch/vochart.h>

#define SWITCH_REPLICAS 1

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
    vochart_server_t *ports_inbound[SWITCH_PORTS];
    vochart_client_t *ports_outbound[SWITCH_PORTS];

    uint8_t routing_table[SWITCH_ROUTES];

    thread_t switch_task;
} switch_t;

void switch_mainloop_internal(switch_t *sw);
void switch_init_internal(switch_t *sw);

#define SWITCH_REGISTER(v_ident)                                                                                 \
    static_repeat(SWITCH_REPLICAS, switch_replica_id) {                                                          \
        extern switch_t symbol_join(v_ident, switch_replica_id);                                                 \
        TASK_REGISTER(symbol_join(v_ident, switch_replica_id, task), "switch_loop",                              \
                      switch_mainloop_internal, &symbol_join(v_ident, switch_replica_id), RESTARTABLE);          \
        switch_t symbol_join(v_ident, switch_replica_id) = {                                                     \
            .ports_inbound = { NULL },                                                                           \
            .ports_outbound = { NULL },                                                                          \
            .routing_table = { 0 },                                                                              \
            .switch_task = &symbol_join(v_ident, switch_replica_id, task),                                       \
        }                                                                                                        \
    }

#define SWITCH_SCHEDULE(v_ident)                                                                     \
    TASK_SCHEDULE(v_ident ## _task, 100)

// inbound is for packets TO the switch; the switch acts as the server.
// outbound is for packets FROM the switch; the switch acts as the client.
// these charts must pass io_rx_ent entries REGARDLESS OF DIRECTION!
#define SWITCH_PORT(v_ident, v_port, v_inbound, v_outbound, v_in_note_size, v_out_note_size, v_note_count)       \
    IO_RX_ASSERT_SIZE(v_in_note_size, 1);                                                                        \
    IO_RX_ASSERT_SIZE(v_out_note_size, 1);                                                                       \
    static_assert(SWITCH_PORT_BASE <= (v_port) && (v_port) < SWITCH_PORT_BASE + SWITCH_PORTS,                    \
                  "switch port must be valid");                                                                  \
    static_repeat(SWITCH_REPLICAS, switch_replica_id) {                                                          \
        VOCHART_SERVER(v_inbound,  switch_replica_id, 1, v_in_note_size, v_note_count,                           \
                       task_rouse, symbol_join(v_ident, switch_replica_id).switch_task);                         \
        VOCHART_CLIENT(v_outbound, switch_replica_id, 1, v_out_note_size, v_note_count,                          \
                       task_rouse, symbol_join(v_ident, switch_replica_id).switch_task);                         \
    }                                                                                                            \
    static void v_ident ## _port_ ## v_port ## _init(void) {                                                     \
        static_repeat(SWITCH_REPLICAS, switch_replica_id) {                                                      \
            assert(symbol_join(v_ident, switch_replica_id).ports_inbound[(v_port) - SWITCH_PORT_BASE] == NULL);  \
            assert(symbol_join(v_ident, switch_replica_id).ports_outbound[(v_port) - SWITCH_PORT_BASE] == NULL); \
            symbol_join(v_ident, switch_replica_id).ports_inbound[(v_port) - SWITCH_PORT_BASE]                   \
                = VOCHART_SERVER_PTR(v_inbound, switch_replica_id);                                              \
            symbol_join(v_ident, switch_replica_id).ports_outbound[(v_port) - SWITCH_PORT_BASE]                  \
                = VOCHART_CLIENT_PTR(v_outbound, switch_replica_id);                                             \
        }                                                                                                        \
    }                                                                                                            \
    PROGRAM_INIT(STAGE_RAW, v_ident ## _port_ ## v_port ## _init)

#define SWITCH_ROUTE(v_ident, v_logical_address, v_port, v_address_pop)                              \
    static_assert(SWITCH_ROUTE_BASE <= (v_logical_address), "switch route must be valid");           \
    static_assert(SWITCH_PORT_BASE <= (v_port) && (v_port) < SWITCH_PORT_BASE + SWITCH_PORTS,        \
                  "switch port must be valid");                                                      \
    static void v_ident ## _route_ ## v_logical_address ## _init(void) {                             \
        uint8_t route = (v_port) | SWITCH_ROUTE_FLAG_ENABLED;                                        \
        if (v_address_pop) {                                                                         \
            route |= SWITCH_ROUTE_FLAG_POP;                                                          \
        }                                                                                            \
        assert((route & SWITCH_ROUTE_PORT_MASK) == (v_port));                                        \
        static_repeat(SWITCH_REPLICAS, switch_replica_id) {                                          \
            assert(symbol_join(v_ident, switch_replica_id)                                           \
                        .routing_table[(v_logical_address) - SWITCH_ROUTE_BASE] == 0);               \
            symbol_join(v_ident, switch_replica_id)                                                  \
                        .routing_table[(v_logical_address) - SWITCH_ROUTE_BASE] = route;             \
        }                                                                                            \
    }                                                                                                \
    PROGRAM_INIT(STAGE_RAW, v_ident ## _route_ ## v_logical_address ## _init)

#endif /* FSW_FAKEWIRE_RMAP_H */
