#include <string.h>

#include <hal/atomic.h>
#include <fsw/debug.h>
#include <fsw/io.h>
#include <fsw/fakewire/switch.h>

void switch_init_internal(void *opaque) {
    assert(opaque != NULL);
    switch_t *sw = (switch_t *) opaque;

    semaphore_init(&sw->switching_wake);
}

// returns TRUE if packet is consumed.
static bool switch_packet(switch_t *sw, int port, chart_index_t avail_count, struct io_rx_ent *entry,
                          chart_t **outbound_out) {
    // make sure we have a destination
    if (entry->actual_length == 0) {
        debugf(WARNING, "Switch port %u: dropping empty packet.", port);
        return true;
    }
    uint8_t destination = entry->data[0];
    if (destination < SWITCH_PORT_BASE) {
        debugf(WARNING, "Switch port %u: dropping packet (len=%u) to invalid address %u.",
               port, entry->actual_length, destination);
        return true;
    }
    bool address_pop = true;
    int outport = destination;
    if (destination >= SWITCH_ROUTE_BASE) {
        assert(destination - SWITCH_ROUTE_BASE < SWITCH_ROUTES);
        uint8_t route = sw->routing_table[destination - SWITCH_ROUTE_BASE];
        if (!(route & SWITCH_ROUTE_FLAG_ENABLED)) {
            debugf(WARNING, "Switch port %u: dropping packet (len=%u) to nonexistent route %u.",
                   port, entry->actual_length, destination);
            return true;
        }
        if (!(route & SWITCH_ROUTE_FLAG_POP)) {
            address_pop = false;
        }
        outport = (route & SWITCH_ROUTE_PORT_MASK);
        assert(SWITCH_PORT_BASE <= outport && outport < SWITCH_PORT_BASE + SWITCH_PORTS);
    }
    assert(SWITCH_PORT_BASE <= outport && outport < SWITCH_PORT_BASE + SWITCH_PORTS);
    chart_t *outbound = atomic_load(sw->ports_outbound[outport - SWITCH_PORT_BASE]);
    if (!outbound) {
        debugf(WARNING,
               "Switch port %u: dropping packet (len=%u) to nonexistent port %u (address=%u).",
               port, entry->actual_length, outport, destination);
        return true;
    }
    struct io_rx_ent *entry_out = chart_request_start(outbound);
    if (!entry_out) {
        // can't send right now.

        // if we have more packets blocked behind this one, we don't want to make them wait for this one to be
        // sendable. so if we can't forward it, and we have more backed up, then drop it.
        if (avail_count > 1) {
            debugf(WARNING, "Switch port %u: dropping packet (len=%u) to backlogged port %u (address=%u).",
                   port, entry->actual_length, outport, destination);
            return true;
        }

        // alternatively, if this is the only packet, we can just wait until delivery is possible. if we get more
        // packets behind it, and still can't transmit it, then we'll drop it then.
        return false;
    }
    entry_out->receive_timestamp = entry->receive_timestamp;
    entry_out->actual_length = entry->actual_length;
    uint8_t *source = entry->data;
    if (address_pop) {
        // drop the first address
        entry_out->actual_length -= 1;
        source += 1;
    }
    if (entry_out->actual_length > io_rx_size(outbound)) {
        // don't passively accept this; it's likely to cause trouble down the line if left like this. so report it.
        debugf(WARNING, "Switch port %u: dropping packet (len=%u) due to truncation (maxlen=%u) by "
               "target port %u (address=%u).", port, entry->actual_length, io_rx_size(outbound), outport, destination);
        return true;
    }
    memcpy(entry_out->data, entry->data, entry_out->actual_length);
    // defer chart_request_send(outbound, 1) until chart_reply_send(inbound, 1) has completed. see later.
    *outbound_out = outbound;
    return true;
}

void switch_mainloop_internal(void *opaque) {
    assert(opaque != NULL);
    switch_t *sw = (switch_t *) opaque;

    for (;;) {
        // attempt to perform transfer for each port
        struct io_rx_ent *entry;
        bool made_progress = false;
        for (int port = SWITCH_PORT_BASE; port < SWITCH_PORT_BASE + SWITCH_PORTS; port++) {
            chart_t *inbound = atomic_load(sw->ports_inbound[port - SWITCH_PORT_BASE]);
            if (inbound != NULL && (entry = chart_reply_start(inbound)) != NULL) {
                assert(entry->actual_length <= io_rx_size(inbound));
                chart_t *outbound = NULL;
                if (switch_packet(sw, port, chart_reply_avail(inbound), entry, &outbound)) {
                    chart_reply_send(inbound, 1);
                    // we have to do this AFTER we acknowledge the original sender... it's much worse for us to
                    // duplicate a packet than for us to drop a packet! so if we restart between the two sends, we want
                    // to make sure the packet is dropped, not duplicated.
                    if (outbound) {
                        chart_request_send(outbound, 1);
                    }
                    made_progress = true;
                }
            }
        }
        if (!made_progress) {
            semaphore_take(&sw->switching_wake);
        }
    }
}

static void switch_notify_loop(void *opaque) {
    assert(opaque != NULL);
    switch_t *sw = (switch_t *) opaque;

    // if semaphore has already been notified, that's fine; no need to do it again.
    (void) semaphore_give(&sw->switching_wake);
}

void switch_add_port(switch_t *sw, uint8_t port_number, chart_t *inbound, chart_t *outbound) {
    assert(sw != NULL && inbound != NULL && outbound != NULL);
    assert(SWITCH_PORT_BASE <= port_number && port_number < SWITCH_PORT_BASE + SWITCH_PORTS);
    assert(io_rx_size(inbound) > 0);
    assert(io_rx_size(outbound) > 0);

    assert(sw->ports_inbound[port_number - SWITCH_PORT_BASE] == NULL);
    assert(sw->ports_outbound[port_number - SWITCH_PORT_BASE] == NULL);

    chart_attach_server(inbound, switch_notify_loop, sw);
    chart_attach_client(outbound, switch_notify_loop, sw);

    atomic_store(sw->ports_inbound[port_number - SWITCH_PORT_BASE], inbound);
    atomic_store(sw->ports_outbound[port_number - SWITCH_PORT_BASE], outbound);
}

void switch_add_route(switch_t *sw, uint8_t logical_address, uint8_t port_number, bool address_pop) {
    assert(sw != NULL);
    assertf(SWITCH_ROUTE_BASE <= logical_address,
            "route_base=%u, logical_address=%u", SWITCH_ROUTE_BASE, logical_address);
    assert(SWITCH_PORT_BASE <= port_number && port_number <= SWITCH_PORT_BASE + SWITCH_PORTS);

    assert(sw->routing_table[logical_address - SWITCH_ROUTE_BASE] == 0);
    uint8_t route = port_number | SWITCH_ROUTE_FLAG_ENABLED;
    if (address_pop) {
        route |= SWITCH_ROUTE_FLAG_POP;
    }
    assert((route & SWITCH_ROUTE_PORT_MASK) == port_number);
    sw->routing_table[logical_address - SWITCH_ROUTE_BASE] = route;
}
