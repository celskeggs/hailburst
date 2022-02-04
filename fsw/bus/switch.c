#include <string.h>

#include <hal/atomic.h>
#include <hal/debug.h>
#include <synch/io.h>
#include <bus/switch.h>

//#define SWITCH_DEBUG

// returns TRUE if packet is consumed.
static bool switch_packet(switch_t *sw, int port, chart_index_t avail_count, struct io_rx_ent *entry,
                          vochart_client_t **outbound_out) {
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
    vochart_client_t *outbound = atomic_load(sw->ports_outbound[outport - SWITCH_PORT_BASE]);
    if (!outbound) {
        debugf(WARNING,
               "Switch port %u: dropping packet (len=%u) to nonexistent port %u (address=%u).",
               port, entry->actual_length, outport, destination);
        return true;
    }
    struct io_rx_ent *entry_out = vochart_request_start(outbound);
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
        debugf(TRACE, "Switch port %u: holding packet (len=%u) until port %u (address=%u) is free.",
               port, entry->actual_length, outport, destination);
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
    if (entry_out->actual_length > io_rx_size_vc(outbound)) {
        // don't passively accept this; it's likely to cause trouble down the line if left like this. so report it.
        debugf(WARNING, "Switch port %u: dropping packet (len=%u) due to truncation (maxlen=%u) by target "
               "port %u (address=%u).", port, entry->actual_length, io_rx_size_vc(outbound), outport, destination);
        return true;
    }
    memcpy(entry_out->data, entry->data, entry_out->actual_length);
    // defer chart_request_send(outbound, 1) until chart_reply_send(inbound, 1) has completed. see later.
    *outbound_out = outbound;
    debugf(TRACE, "Switch port %u: forwarding packet (len=%u) to destination port %u (address=%u).",
           port, entry->actual_length, outport, destination);
    return true;
}

void switch_mainloop_internal(switch_t *sw) {
    assert(sw != NULL);

    for (;;) {
        // attempt to perform transfer for each port
        struct io_rx_ent *entry;
        unsigned int packets = 0;
        for (int port = SWITCH_PORT_BASE; port < SWITCH_PORT_BASE + SWITCH_PORTS; port++) {
            vochart_server_t *inbound = atomic_load(sw->ports_inbound[port - SWITCH_PORT_BASE]);
            if (inbound != NULL && (entry = vochart_reply_start(inbound)) != NULL) {
                assert(entry->actual_length <= io_rx_size_vs(inbound));
                vochart_client_t *outbound = NULL;
                if (switch_packet(sw, port, vochart_reply_avail(inbound), entry, &outbound)) {
                    vochart_reply_send(inbound);
                    // we have to do this AFTER we acknowledge the original sender... it's much worse for us to
                    // duplicate a packet than for us to drop a packet! so if we restart between the two sends, we want
                    // to make sure the packet is dropped, not duplicated.
                    if (outbound) {
                        vochart_request_send(outbound);
                    }
                    packets++;
                }
            }
        }
        if (packets > 0) {
#ifdef SWITCH_DEBUG
            debugf(TRACE, "Switch routed %u packets; checking to see if there are any more.", packets);
#endif
        } else {
#ifdef SWITCH_DEBUG
            debugf(TRACE, "Switch dozing; no packets to route right now.");
#endif
            task_doze();
#ifdef SWITCH_DEBUG
            debugf(TRACE, "Switch roused!");
#endif
        }
    }
}
