#include <string.h>

#include <fsw/clock.h>
#include <fsw/debug.h>
#include <fsw/io.h>
#include <fsw/fakewire/rmap.h>

enum {
    PROTOCOL_RMAP = 0x01,

    // time out transmits after two milliseconsd
    RMAP_TRANSMIT_TIMEOUT_NS = 2 * 1000 * 1000,
    // time out receives after two milliseconds, which is nearly 4x the average time for a transaction.
    RMAP_RECEIVE_TIMEOUT_NS = 2 * 1000 * 1000,
};

void rmap_notify_wake(rmap_t *rmap) {
    assert(rmap != NULL);

    local_rouse(rmap->client_task);
}

static void rmap_cancel_active_work(rmap_t *rmap) {
    assert(rmap != NULL);
    if (rmap->current_routing != NULL) {
        debugf(WARNING, "RMAP WRITE ABORT: DEST=%u SRC=%u KEY=%u",
               rmap->current_routing->destination.logical_address, rmap->current_routing->source.logical_address,
               rmap->current_routing->dest_key);
        rmap->current_routing = NULL;
    }
    if (rmap->lingering_read) {
        chart_reply_send(rmap->rx_chart, 1);
        rmap->lingering_read = false;
    }
}

rmap_status_t rmap_write_prepare(rmap_t *rmap, const rmap_addr_t *routing, rmap_flags_t flags,
                                 uint8_t ext_addr, uint32_t main_addr, uint8_t **ptr_out) {
    // make sure we didn't get any null pointers
    assert(rmap != NULL && routing != NULL);
    // make sure flags are valid
    assert(flags == (flags & (RF_VERIFY | RF_ACKNOWLEDGE | RF_INCREMENT)));

    // clear up anything ongoing
    rmap_cancel_active_work(rmap);

    debugf(TRACE, "RMAP WRITE START: DEST=%u SRC=%u KEY=%u FLAGS=%x ADDR=0x%02x_%08x",
           routing->destination.logical_address, routing->source.logical_address, routing->dest_key,
           flags, ext_addr, main_addr);

    struct io_rx_ent *entry = chart_request_start(rmap->tx_chart);
    if (entry == NULL) {
        // indicates that the entire outgoing queue is full... this is very odd, because the switch should drop the
        // first packet if there's a second packet waiting behind it!
        debugf(WARNING, "RMAP WRITE  STOP: DEST=%u SRC=%u KEY=%u STATUS=%u",
               routing->destination.logical_address, routing->source.logical_address, routing->dest_key,
               RS_TRANSMIT_BLOCKED);
        *ptr_out = NULL;
        return RS_TRANSMIT_BLOCKED;
    }
    uint8_t *out = entry->data;
    memset(out, 0, io_rx_size(rmap->tx_chart));
    // and then start writing output bytes according to the write command format
    if (routing->destination.num_path_bytes > 0) {
        assert(routing->destination.num_path_bytes <= RMAP_MAX_PATH);
        assert(routing->destination.path_bytes != NULL);
        memcpy(out, routing->destination.path_bytes, routing->destination.num_path_bytes);
        out += routing->destination.num_path_bytes;
    }
    uint8_t *header_region = out;
    *out++ = routing->destination.logical_address;
    *out++ = PROTOCOL_RMAP;
    int spal = (routing->source.num_path_bytes + 3) / 4;
    assert((spal & RF_SOURCEPATH) == spal);
    uint8_t txn_flags = RF_COMMAND | RF_WRITE | flags | spal;
    *out++ = txn_flags;
    *out++ = routing->dest_key;
    rmap_encode_source_path(&out, &routing->source);
    *out++ = routing->source.logical_address;

    rmap->current_txn_flags = txn_flags;
    rmap->current_txn_id += 1;
    rmap->current_routing = routing;

    *out++ = (rmap->current_txn_id >> 8) & 0xFF;
    *out++ = (rmap->current_txn_id >> 0) & 0xFF;
    *out++ = ext_addr;
    *out++ = (main_addr >> 24) & 0xFF;
    *out++ = (main_addr >> 16) & 0xFF;
    *out++ = (main_addr >> 8) & 0xFF;
    *out++ = (main_addr >> 0) & 0xFF;
    // compute the header CRC for everything EXCEPT the final three data bytes
    uint8_t header_crc_partial = rmap_crc8(header_region, out - header_region);
    // skip three bytes for the data length; we'll come back and write this later.
    out += 3;
    // and now insert the (partial) header CRC
    *out++ = header_crc_partial;

    // record our current pointer, so that we can finish generating the rest afterwards
    rmap->body_pointer = out;
    // and provide the pointer to the caller, so that it can populate the data for this packet.
    *ptr_out = out;
    // the transaction hasn't completed, but everything is fine so far.
    return RS_OK;
}

static bool rmap_transmit_pending(rmap_t *rmap) {
    assert(rmap != NULL);
    // once all packets have been forwarded by the virtual switch, avail will equal count
    return chart_request_avail(rmap->tx_chart) < chart_note_count(rmap->tx_chart);
}

static void rmap_drop_packets(rmap_t *rmap) {
    chart_index_t packets;
    while ((packets = chart_reply_avail(rmap->rx_chart)) > 0) {
        debugf(WARNING, "Dropping %u packets because no request was in progress.", packets);
        chart_reply_send(rmap->rx_chart, packets);
    }
}

struct write_reply {
    bool     received;
    uint8_t  status_byte;
    uint64_t receive_timestamp;
};

// returns true if packet is a valid reply, and false otherwise.
static bool rmap_validate_write_reply(rmap_t *rmap, uint8_t *in, size_t count, struct write_reply *out) {
    assert(rmap != NULL && in != NULL && out != NULL);
    // validate basic parameters of a valid RMAP packet
    if (count < 8) {
        debugf(WARNING, "Dropped truncated packet (len=%u).", count);
        return false;
    }
    if (in[1] != PROTOCOL_RMAP) {
        debugf(WARNING, "Dropped non-RMAP packet (len=%u, proto=%u).", count, in[1]);
        return false;
    }
    // validate that this is the correct type of RMAP packet
    uint8_t flags = in[2];
    if ((flags & (RF_RESERVED | RF_COMMAND | RF_ACKNOWLEDGE | RF_WRITE)) != (RF_ACKNOWLEDGE | RF_WRITE)) {
        debugf(WARNING, "Dropped RMAP packet (len=%u) with invalid flags 0x%02x when pending write.", count, flags);
        return false;
    }
    // validate header integrity (length, CRC)
    if (count != 8) {
        debugf(WARNING, "Dropped packet exceeding RMAP write reply length (len=%u).", count);
        return false;
    }
    uint8_t computed_crc = rmap_crc8(in, 7);
    if (computed_crc != in[7]) {
        debugf(WARNING, "Dropped RMAP write reply with invalid CRC (found=0x%02x, expected=0x%02x).",
               computed_crc, in[7]);
        return false;
    }
    // verify transaction ID and flags
    uint16_t txn_id = (in[5] << 8) | in[6];
    if (txn_id != rmap->current_txn_id) {
        debugf(WARNING, "Dropped RMAP write reply with wrong transaction ID (found=0x%04x, expected=0x%04x).",
               txn_id, rmap->current_txn_id);
        return false;
    }
    if ((flags | RF_COMMAND) != rmap->current_txn_flags) {
        debugf(WARNING, "Dropped RMAP write reply with wrong flags (found=0x%02x, expected=0x%02x).",
               flags, rmap->current_txn_flags & ~RF_COMMAND);
        return false;
    }
    // make sure routing addresses match
    const rmap_addr_t *routing = rmap->current_routing;
    assert(routing != NULL);
    if (in[0] != routing->source.logical_address || in[4] != routing->destination.logical_address) {
        debugf(WARNING, "Dropped RMAP write reply with invalid addressing (%u <- %u but expected %u <- %u).",
               in[0], in[4], routing->source.logical_address, routing->destination.logical_address);
        return false;
    }
    out->status_byte = in[3];
    return true;
}

// pull all received packets until a valid packet is found. return true if found, false if not.
static bool rmap_pull_write_reply(rmap_t *rmap, struct write_reply *out) {
    assert(rmap != NULL && out != NULL);

    struct io_rx_ent *ent;
    while (!out->received && (ent = chart_reply_start(rmap->rx_chart)) != NULL) {
        assert(ent->actual_length <= io_rx_size(rmap->rx_chart));
        assert(ent->receive_timestamp > 0);

        if (rmap_validate_write_reply(rmap, ent->data, ent->actual_length, out)) {
            // packet is a valid write reply
            out->receive_timestamp = ent->receive_timestamp;
            out->received = true;
        }

        chart_reply_send(rmap->rx_chart, 1);
    }
    return out->received;
}

// perform write, with the specified data length.
rmap_status_t rmap_write_commit(rmap_t *rmap, size_t data_length, uint64_t *ack_timestamp_out) {
    assert(rmap != NULL);
    const rmap_addr_t *routing = rmap->current_routing;
    assert(routing != NULL);
    assert(rmap->body_pointer != NULL);
    assert(!rmap->lingering_read);
    assert(data_length <= io_rx_size(rmap->tx_chart) - SCRATCH_MARGIN_WRITE);

    struct io_rx_ent *entry = chart_request_start(rmap->tx_chart);
    assert(entry != NULL);
    // make sure the pointer is coherent
    assert(rmap->body_pointer >= entry->data + 16 && rmap->body_pointer <= entry->data + 16 + RMAP_MAX_PATH * 2);
    // backfill length field
    assert((data_length >> 24) == 0); // should already be guaranteed by previous checks, but just in case
    rmap->body_pointer[-4] = (data_length >> 16) & 0xFF;
    rmap->body_pointer[-3] = (data_length >> 8) & 0xFF;
    rmap->body_pointer[-2] = (data_length >> 0) & 0xFF;
    // add the remaining three bytes to the header CRC
    rmap->body_pointer[-1] = rmap_crc8_extend(rmap->body_pointer[-1], &rmap->body_pointer[-4], 3);
    // now add the data CRC as a trailer
    rmap->body_pointer[data_length] = rmap_crc8(rmap->body_pointer, data_length);

    // compute final length
    entry->actual_length = &rmap->body_pointer[data_length + 1] - entry->data;
    assert(entry->actual_length <= io_rx_size(rmap->tx_chart));
    // clear receive timestamp, because it doesn't matter for outbound packets
    entry->receive_timestamp = 0;

    // before we transmit, make sure to get rid of any packets we already have in our receive queue, because those
    // are necessarily not the correct reply.
    rmap_drop_packets(rmap);

    // now transmit!
    chart_request_send(rmap->tx_chart, 1);

    struct write_reply write_reply = { .received = false };

    // wait for packet to be forwarded by the virtual switch and disappear from our buffer
    uint64_t transmit_timeout = clock_timestamp_monotonic() + RMAP_TRANSMIT_TIMEOUT_NS;
    while (rmap_transmit_pending(rmap) && clock_timestamp_monotonic() < transmit_timeout) {
        // make sure we discard any invalid packets we receive, so that our actual packet doesn't get dropped
        if (rmap_pull_write_reply(rmap, &write_reply)) {
            // woah! we already have a reply? that's great! just make sure it's actually possible.
            if (rmap_transmit_pending(rmap)) {
                // should not physically be possible for us to have received this packet already; it MUST be invalid.
                debugf(CRITICAL, "Time travel! Packet reply received before request sent!");
                write_reply.received = false;
                continue;
            }
            // we did get a response! skip the rest of this timeout.
            break;
        }
        (void) local_doze_timed_abs(rmap->client_task, transmit_timeout);
    }

    // exactly how we determine the final status depends on whether we expect a reply from the remote device.
    rmap_status_t status_out;

    uint64_t ack_timestamp = 0;

    if (rmap_transmit_pending(rmap)) {
        // timed out when transmitting request, so don't bother waiting for a reply
        assert(clock_timestamp_monotonic() >= transmit_timeout);
        status_out = RS_TRANSMIT_TIMEOUT;
        // TODO: could we proactively invalidate the message now, so that it doesn't get transmitted later?
    } else if (rmap->current_txn_flags & RF_ACKNOWLEDGE) {
        // if an acknowledgement was requested, then we need to wait for a reply!

        uint64_t timeout = clock_timestamp_monotonic() + RMAP_RECEIVE_TIMEOUT_NS;
        while (clock_timestamp_monotonic() < timeout && !rmap_pull_write_reply(rmap, &write_reply)) {
            (void) local_doze_timed_abs(rmap->client_task, timeout);
        }

        if (rmap_pull_write_reply(rmap, &write_reply)) {
            // got a reply!
            status_out = write_reply.status_byte;
            ack_timestamp = write_reply.receive_timestamp;
            // drop any remaining packets
            rmap_drop_packets(rmap);
        } else {
            // timed out!
            assert(clock_timestamp_monotonic() > timeout);
            status_out = RS_TRANSACTION_TIMEOUT;
        }
    } else {
        // if we transmitted successfully, but didn't ask for a reply, we can just assume the request succeeded!

        status_out = RS_OK;

        if (rmap_pull_write_reply(rmap, &write_reply)) {
            // this should not happen, unless a packet got corrupted somehow and confused for a valid reply, so report
            // it as an error.
            debugf(CRITICAL, "Impossible RMAP receive; must have gotten a corrupted packet mixed up with a real one.");
            // BUT this doesn't really mean anything bad for our message, so we don't need to change status_out.
        }
    }

    debugf(TRACE, "RMAP WRITE  STOP: DEST=%u SRC=%u KEY=%u STATUS=%u",
           routing->destination.logical_address, routing->source.logical_address, routing->dest_key, status_out);

    rmap->body_pointer = NULL;
    rmap->current_txn_flags = 0;
    // don't reset current_txn_id, because it's used to track the next transaction ID to use
    rmap->current_routing = NULL;

    if (ack_timestamp_out != NULL) {
        *ack_timestamp_out = ack_timestamp;
    }
    return status_out;
}

rmap_status_t rmap_write_exact(rmap_t *rmap, const rmap_addr_t *routing, rmap_flags_t flags,
                               uint8_t ext_addr, uint32_t main_addr, size_t length, uint8_t *input,
                               uint64_t *ack_timestamp_out) {
    rmap_status_t status;

    assert(length <= io_rx_size(rmap->tx_chart) - SCRATCH_MARGIN_WRITE);

    uint8_t *ptr_write = NULL;
    status = rmap_write_prepare(rmap, routing, flags, ext_addr, main_addr, &ptr_write);
    if (status != RS_OK) {
        if (ack_timestamp_out != NULL) {
            *ack_timestamp_out = 0;
        }
        return status;
    }

    assert(ptr_write != NULL);
    memcpy(ptr_write, input, length);

    return rmap_write_commit(rmap, length, ack_timestamp_out);
}

struct read_reply {
    bool     received;
    uint8_t  status_byte;
    uint32_t data_length;
    uint8_t *data_ptr;
};

// returns true if packet is a valid reply, and false otherwise.
static bool rmap_validate_read_reply(rmap_t *rmap, uint8_t *in, size_t count, const rmap_addr_t *routing,
                                     struct read_reply *out) {
    assert(rmap != NULL && in != NULL && routing != NULL && out != NULL);
    // validate basic parameters of a valid RMAP packet
    if (count < 8) {
        debugf(WARNING, "Dropped truncated packet (len=%u).", count);
        return false;
    }
    if (in[1] != PROTOCOL_RMAP) {
        debugf(WARNING, "Dropped non-RMAP packet (len=%u, proto=%u).", count, in[1]);
        return false;
    }
    // validate that this is the correct type of RMAP packet
    uint8_t flags = in[2];
    if ((flags & (RF_RESERVED | RF_COMMAND | RF_ACKNOWLEDGE | RF_VERIFY | RF_WRITE)) != RF_ACKNOWLEDGE) {
        debugf(WARNING, "Dropped RMAP packet (len=%u) with invalid flags 0x%02x when pending read.", count, flags);
        return false;
    }
    // validate header integrity (length, CRC)
    if (count < 13) {
        debugf(WARNING, "Dropped truncated RMAP read reply packet (len=%u).", count);
        return false;
    }
    uint8_t computed_crc = rmap_crc8(in, 11);
    if (computed_crc != in[11]) {
        debugf(WARNING, "Dropped RMAP read reply with invalid header CRC (found=0x%02x, expected=0x%02x).",
               computed_crc, in[11]);
        return false;
    }
    if (in[7] != 0) {
        debugf(WARNING, "Dropped invalid RMAP read reply with nonzero reserved byte (%u).", in[7]);
        return false;
    }
    // second, validate full length and data CRC after parsing data length.
    uint32_t data_length = (in[8] << 16) | (in[9] << 8) | in[10];
    if (count != 13 + data_length) {
        debugf(WARNING, "Dropped RMAP read reply with mismatched data length field (found=%u, expected=%u).",
               data_length, count - 13);
        return false;
    }
    uint8_t data_crc = rmap_crc8(&in[12], data_length);
    if (data_crc != in[count - 1]) {
        debugf(WARNING, "Dropped RMAP read reply with invalid data CRC (found=0x%02x, expected=0x%02x).",
               data_crc, in[count - 1]);
        return false;
    }
    // verify transaction ID and flags
    uint16_t txn_id = (in[5] << 8) | in[6];
    if (txn_id != rmap->current_txn_id) {
        debugf(WARNING, "Dropped RMAP read reply with wrong transaction ID (found=0x%04x, expected=0x%04x).",
               txn_id, rmap->current_txn_id);
        return false;
    }
    if ((flags | RF_COMMAND) != rmap->current_txn_flags) {
        debugf(WARNING, "Dropped RMAP read reply with wrong flags (found=0x%02x, expected=0x%02x).",
               flags, rmap->current_txn_flags & ~RF_COMMAND);
        return false;
    }
    // make sure routing addresses match
    if (in[0] != routing->source.logical_address || in[4] != routing->destination.logical_address) {
        debugf(WARNING, "Dropped RMAP write reply with invalid addressing (%u <- %u but expected %u <- %u).",
               in[0], in[4], routing->source.logical_address, routing->destination.logical_address);
        return false;
    }
    out->status_byte = in[3];
    out->data_length = data_length;
    out->data_ptr = &in[12];
    return true;
}

// pulls all readable packets until valid packet is found. returns true if found, false if not.
static bool rmap_pull_read_reply(rmap_t *rmap, const rmap_addr_t *routing, struct read_reply *out) {
    assert(rmap != NULL && out != NULL);

    if (out->received) {
        return true;
    }

    struct io_rx_ent *ent;
    while ((ent = chart_reply_start(rmap->rx_chart)) != NULL) {
        assert(ent->actual_length <= io_rx_size(rmap->rx_chart));
        assert(ent->receive_timestamp > 0);

        if (rmap_validate_read_reply(rmap, ent->data, ent->actual_length, routing, out)) {
            // packet is a valid read reply
            out->received = true;
            return true;
        }

        chart_reply_send(rmap->rx_chart, 1);
    }
    return false;
}

rmap_status_t rmap_read_fetch(rmap_t *rmap, const rmap_addr_t *routing, rmap_flags_t flags,
                              uint8_t ext_addr, uint32_t main_addr, size_t *length, uint8_t **ptr_out) {
    // make sure we didn't get any null pointers
    assert(rmap != NULL && routing != NULL && ptr_out != NULL && length != NULL);
    // make sure flags are valid
    assert(flags == (flags & RF_INCREMENT));
    // make sure that the receive chart has enough space to buffer this much data in scratch memory when receiving
    uint32_t max_data_length = *length;
    assert(0 < max_data_length && max_data_length <= RMAP_MAX_DATA_LEN);
    assert(max_data_length + SCRATCH_MARGIN_READ <= io_rx_size(rmap->rx_chart));

    // clear up anything ongoing
    rmap_cancel_active_work(rmap);

    debugf(TRACE, "RMAP  READ START: DEST=%u SRC=%u KEY=%u FLAGS=%x ADDR=0x%02x_%08x REQLEN=%zu",
           routing->destination.logical_address, routing->source.logical_address, routing->dest_key,
           flags, ext_addr, main_addr, max_data_length);

    struct io_rx_ent *entry = chart_request_start(rmap->tx_chart);
    if (entry == NULL) {
        // indicates that the entire outgoing queue is full... this is very odd, because the switch should drop the
        // first packet if there's a second packet waiting behind it!
        debugf(WARNING, "RMAP  READ  STOP: DEST=%u SRC=%u KEY=%u STATUS=%u",
               routing->destination.logical_address, routing->source.logical_address, routing->dest_key,
               RS_TRANSMIT_BLOCKED);
        *length = 0;
        *ptr_out = NULL;
        return RS_TRANSMIT_BLOCKED;
    }
    uint8_t *out = entry->data;
    memset(out, 0, io_rx_size(rmap->tx_chart));
    // now start writing output bytes according to the read command format
    if (routing->destination.num_path_bytes > 0) {
        assert(routing->destination.num_path_bytes <= RMAP_MAX_PATH);
        assert(routing->destination.path_bytes != NULL);
        memcpy(out, routing->destination.path_bytes, routing->destination.num_path_bytes);
        out += routing->destination.num_path_bytes;
    }
    uint8_t *header_region = out;
    *out++ = routing->destination.logical_address;
    *out++ = PROTOCOL_RMAP;
    int spal = (routing->source.num_path_bytes + 3) / 4;
    assert((spal & RF_SOURCEPATH) == spal);
    uint8_t txn_flags = RF_COMMAND | RF_ACKNOWLEDGE | flags | spal;
    *out++ = txn_flags;
    *out++ = routing->dest_key;
    rmap_encode_source_path(&out, &routing->source);
    *out++ = routing->source.logical_address;

    rmap->current_txn_flags = txn_flags;
    rmap->current_txn_id += 1;

    *out++ = (rmap->current_txn_id >> 8) & 0xFF;
    *out++ = (rmap->current_txn_id >> 0) & 0xFF;
    *out++ = ext_addr;
    *out++ = (main_addr >> 24) & 0xFF;
    *out++ = (main_addr >> 16) & 0xFF;
    *out++ = (main_addr >> 8) & 0xFF;
    *out++ = (main_addr >> 0) & 0xFF;
    assert(((max_data_length >> 24) & 0xFF) == 0); // should already be guaranteed by previous checks, but just in case
    *out++ = (max_data_length >> 16) & 0xFF;
    *out++ = (max_data_length >> 8) & 0xFF;
    *out++ = (max_data_length >> 0) & 0xFF;
    // and then compute the header CRC
    uint8_t header_crc = rmap_crc8(header_region, out - header_region);
    *out++ = header_crc;

    // compute final length
    entry->actual_length = out - entry->data;
    // clear receive timestamp, because it doesn't matter for outbound packets
    entry->receive_timestamp = 0;

    // before we transmit, make sure to get rid of any packets we already have in our receive queue, because those
    // are necessarily not the correct reply.
    rmap_drop_packets(rmap);

    // now transmit!
    chart_request_send(rmap->tx_chart, 1);

    struct read_reply read_reply = { .received = false };

    // wait for packet to be forwarded by the virtual switch and disappear from our buffer
    uint64_t transmit_timeout = clock_timestamp_monotonic() + RMAP_TRANSMIT_TIMEOUT_NS;
    while (rmap_transmit_pending(rmap) && clock_timestamp_monotonic() < transmit_timeout) {
        // make sure we discard any invalid packets we receive, so that our actual packet doesn't get dropped
        if (rmap_pull_read_reply(rmap, routing, &read_reply)) {
            // woah! we already have a reply? that's great! just make sure it's actually possible.
            if (rmap_transmit_pending(rmap)) {
                // should not physically be possible for us to have received this packet already; it MUST be invalid.
                debugf(CRITICAL, "Time travel! Packet reply received before request sent!");
                read_reply.received = false;
                continue;
            }
            // we did get a response! skip the rest of this timeout.
            break;
        }
        (void) local_doze_timed_abs(rmap->client_task, transmit_timeout);
    }

    rmap_status_t status_out;

    if (rmap_transmit_pending(rmap)) {
        // timed out when transmitting request, so don't bother waiting for a reply
        assert(clock_timestamp_monotonic() > transmit_timeout);
        status_out = RS_TRANSMIT_TIMEOUT;
        *length = 0;
        *ptr_out = NULL;
        // TODO: could we proactively invalidate the message now, so that it doesn't get transmitted later?
    } else {
        // next: wait for a reply!
        uint64_t timeout = clock_timestamp_monotonic() + RMAP_RECEIVE_TIMEOUT_NS;
        while (clock_timestamp_monotonic() < timeout && !rmap_pull_read_reply(rmap, routing, &read_reply)) {
            (void) local_doze_timed_abs(rmap->client_task, timeout);
        }

        if (rmap_pull_read_reply(rmap, routing, &read_reply)) {
            // got a reply!
            status_out = read_reply.status_byte;
            // length already validated
            *length = read_reply.data_length;
            *ptr_out = read_reply.data_ptr;
            // if the length doesn't match the expected length, signal an error (but still return the pointer)
            if (read_reply.data_length != max_data_length && status_out == RS_OK) {
                status_out = RS_READ_LENGTH_DIFFERS;
            }
            // delay consuming packet until data is used
            assert(!rmap->lingering_read);
            rmap->lingering_read = true;
        } else {
            // timed out!
            assert(clock_timestamp_monotonic() > timeout);
            status_out = RS_TRANSACTION_TIMEOUT;
            *length = 0;
            *ptr_out = NULL;
        }
    }

    debugf(TRACE, "RMAP  READ  STOP: DEST=%u SRC=%u KEY=%u LEN=%zu STATUS=%u",
           routing->destination.logical_address, routing->source.logical_address, routing->dest_key,
           *length, status_out);

    return status_out;
}

rmap_status_t rmap_read_exact(rmap_t *rmap, const rmap_addr_t *routing, rmap_flags_t flags,
                              uint8_t ext_addr, uint32_t main_addr, size_t length, uint8_t *output) {
    assert(output != NULL);
    rmap_status_t status;
    size_t actual_length = length;
    uint8_t *read_ptr = NULL;
    status = rmap_read_fetch(rmap, routing, flags, ext_addr, main_addr, &actual_length, &read_ptr);
    if (status != RS_OK) {
        return status;
    }
    assert(actual_length == length);
    assert(read_ptr != NULL);
    memcpy(output, read_ptr, actual_length);
    return RS_OK;
}