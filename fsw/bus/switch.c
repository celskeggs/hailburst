#include <string.h>

#include <hal/atomic.h>
#include <hal/debug.h>
#include <synch/io.h>
#include <bus/switch.h>

//#define SWITCH_DEBUG

// returns TRUE if packet is consumed.
static void switch_packet(switch_t *sw, uint8_t replica_id, int port,
                          size_t message_size, uint64_t timestamp, uint8_t *message_buffer) {
    uint8_t destination = message_buffer[0];
    if (destination < SWITCH_PORT_BASE) {
        debugf(WARNING, "Switch replica %u port %u: dropped packet (len=%zu) to invalid address %u.",
               replica_id, port, message_size, destination);
        return;
    }
    bool address_pop = true;
    int outport = destination;
    if (destination >= SWITCH_ROUTE_BASE) {
        assert(destination - SWITCH_ROUTE_BASE < SWITCH_ROUTES);
        uint8_t route = sw->routing_table[destination - SWITCH_ROUTE_BASE];
        if (!(route & SWITCH_ROUTE_FLAG_ENABLED)) {
            debugf(WARNING, "Switch replica %u port %u: dropped packet (len=%zu) to nonexistent route %u.",
                   replica_id, port, message_size, destination);
            return;
        }
        if (!(route & SWITCH_ROUTE_FLAG_POP)) {
            address_pop = false;
        }
        outport = (route & SWITCH_ROUTE_PORT_MASK);
        assert(SWITCH_PORT_BASE <= outport && outport < SWITCH_PORT_BASE + SWITCH_PORTS);
    }
    assert(SWITCH_PORT_BASE <= outport && outport < SWITCH_PORT_BASE + SWITCH_PORTS);
    duct_t *outbound = sw->ports_outbound[outport - SWITCH_PORT_BASE];
    if (!outbound) {
        debugf(WARNING, "Switch replica %u port %u: dropped packet (len=%zu) to nonexistent port %u (address=%u).",
               replica_id, port, message_size, outport, destination);
        return;
    }
    if (!duct_send_allowed(outbound, replica_id)) {
        debugf(WARNING,
               "Switch replica %u port %u: dropped packet (len=%zu) violating max flow rate to port %u (address=%u).",
               replica_id, port, message_size, outport, destination);
        return;
    }
    if (address_pop) {
        // drop the first address
        message_size -= 1;
        message_buffer += 1;
        if (message_size == 0) {
            debugf(WARNING,
                   "Switch replica %u port %u: dropped packet (len=%zu) with no data beyond destination address %u.",
                   replica_id, port, message_size, destination);
            return;
        }
    }
    assert(message_size > 0);
    if (message_size > duct_message_size(outbound)) {
        // don't passively accept this; it's likely to cause trouble down the line if left like this. so report it.
        debugf(WARNING, "Switch replica %u port %u: dropped packet (len=%zu) due to truncation (maxlen=%zu) by "
               "target port %u (address=%u).",
               replica_id, port, message_size, duct_message_size(outbound), outport, destination);
        return;
    }
    duct_send_message(outbound, replica_id, message_buffer, message_size, timestamp);
    debugf(TRACE, "Switch replica %u port %u: forwarded packet (len=%zu) to destination port %u (address=%u).",
           replica_id, port, message_size, outport, destination);
}

void switch_io_clip(const switch_replica_t *sr) {
    assert(sr != NULL);
    uint8_t replica_id = sr->replica_id;
    switch_t *sw = sr->replica_switch;
    assert(sw != NULL);

    // attempt to perform transfer for each port
    unsigned int packets = 0;

    // first, prepare all transactions
    for (int port = SWITCH_PORT_BASE; port < SWITCH_PORT_BASE + SWITCH_PORTS; port++) {
        duct_t *inbound = sw->ports_inbound[port - SWITCH_PORT_BASE];
        duct_t *outbound = sw->ports_outbound[port - SWITCH_PORT_BASE];
        if (inbound != NULL) {
            duct_receive_prepare(inbound, replica_id);
        }
        if (outbound != NULL) {
            duct_send_prepare(outbound, replica_id);
        }
    }

    // now shuffle all messages
    for (int port = SWITCH_PORT_BASE; port < SWITCH_PORT_BASE + SWITCH_PORTS; port++) {
        duct_t *inbound = sw->ports_inbound[port - SWITCH_PORT_BASE];
        if (inbound != NULL) {
            uint64_t timestamp = 0;
            size_t message_size;
            while ((message_size = duct_receive_message(inbound, replica_id, sr->scratch_buffer, &timestamp)) != 0) {
                assert(message_size <= sw->scratch_buffer_size);
                switch_packet(sw, replica_id, port, message_size, timestamp, sr->scratch_buffer);
                packets++;
            }
        }
    }

    // finally, commit all transactions
    for (int port = SWITCH_PORT_BASE; port < SWITCH_PORT_BASE + SWITCH_PORTS; port++) {
        duct_t *inbound = sw->ports_inbound[port - SWITCH_PORT_BASE];
        duct_t *outbound = sw->ports_outbound[port - SWITCH_PORT_BASE];
        if (inbound != NULL) {
            duct_receive_commit(inbound, replica_id);
        }
        if (outbound != NULL) {
            duct_send_commit(outbound, replica_id);
        }
    }

#ifdef SWITCH_DEBUG
    debugf(TRACE, "Switch routed %u packets in this epoch; waiting until next epoch...", packets);
#endif
}
