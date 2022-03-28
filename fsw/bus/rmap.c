#include <string.h>

#include <hal/clock.h>
#include <hal/debug.h>
#include <synch/io.h>
#include <bus/rmap.h>

//#define RMAP_TRACE

enum {
    PROTOCOL_RMAP = 0x01,
    RMAP_REPLICA_ID = 0,
};

void rmap_epoch_prepare(rmap_txn_t *txn, rmap_t *rmap) {
    assert(txn != NULL && rmap != NULL);
    txn->rmap = rmap;
    duct_send_prepare(&txn->tx_send_txn, rmap->tx_duct, RMAP_REPLICA_ID);
    duct_receive_prepare(&txn->rx_recv_txn, rmap->rx_duct, RMAP_REPLICA_ID);
}

void rmap_epoch_commit(rmap_txn_t *txn) {
    /* make sure that, if we received a packet, we've taken it out of the receive queue to avoid an assert. */
    static_assert(RMAP_MAX_IO_FLOW == 1, "should only be one message accepted per epoch");
    if (duct_receive_message(&txn->rx_recv_txn, NULL, NULL) > 0) {
        debugf(WARNING, "RMAP (%10s) dropped packet received at unexpected time.", txn->rmap->label);
    }

    duct_send_commit(&txn->tx_send_txn);
    duct_receive_commit(&txn->rx_recv_txn);
}

void rmap_write_start(rmap_txn_t *txn, uint8_t ext_addr, uint32_t main_addr, uint8_t *buffer, size_t data_length) {
    assert(txn != NULL);
    rmap_t *rmap = txn->rmap;
    assert(rmap != NULL && buffer != NULL);
    assert(data_length <= duct_message_size(rmap->tx_duct) - SCRATCH_MARGIN_WRITE);

#ifdef RMAP_TRACE
    debugf(TRACE, "RMAP (%10s) WRITE START: ADDR=0x%02x_%08x LEN=0x%zx",
           rmap->label, ext_addr, main_addr, data_length);
#endif

    if (!duct_send_allowed(&txn->tx_send_txn)) {
        abortf("RMAP (%10s) not permitted to transmit another packet during this epoch.", rmap->label);
    }

    uint8_t *out = rmap->scratch;
    memset(out, 0, duct_message_size(rmap->tx_duct));
    // and then start writing output bytes according to the write command format
    if (rmap->routing->destination.num_path_bytes > 0) {
        assert(rmap->routing->destination.num_path_bytes <= RMAP_MAX_PATH);
        assert(rmap->routing->destination.path_bytes != NULL);
        memcpy(out, rmap->routing->destination.path_bytes, rmap->routing->destination.num_path_bytes);
        out += rmap->routing->destination.num_path_bytes;
    }
    uint8_t *header_region = out;
    *out++ = rmap->routing->destination.logical_address;
    *out++ = PROTOCOL_RMAP;
    int spal = (rmap->routing->source.num_path_bytes + 3) / 4;
    assert((spal & RF_SOURCEPATH) == spal);
    *out++ = RF_COMMAND | RF_WRITE | RF_VERIFY | RF_ACKNOWLEDGE | RF_INCREMENT | spal;
    *out++ = rmap->routing->dest_key;
    rmap_encode_source_path(&out, &rmap->routing->source);
    *out++ = rmap->routing->source.logical_address;

    rmap->current_txn_id += 1;

    *out++ = (rmap->current_txn_id >> 8) & 0xFF;
    *out++ = (rmap->current_txn_id >> 0) & 0xFF;
    *out++ = ext_addr;
    *out++ = (main_addr >> 24) & 0xFF;
    *out++ = (main_addr >> 16) & 0xFF;
    *out++ = (main_addr >> 8) & 0xFF;
    *out++ = (main_addr >> 0) & 0xFF;
    assert((data_length >> 24) == 0); // should already be guaranteed by previous checks, but just in case
    *out++ = (data_length >> 16) & 0xFF;
    *out++ = (data_length >> 8) & 0xFF;
    *out++ = (data_length >> 0) & 0xFF;
    // compute the header CRC
    uint8_t header_crc = rmap_crc8(header_region, out - header_region);
    *out++ = header_crc;

    memcpy(out, buffer, data_length);
    out += data_length;

    *out++ = rmap_crc8(buffer, data_length);

    size_t packet_length = out - rmap->scratch;
    assert(packet_length <= duct_message_size(rmap->tx_duct));
    duct_send_message(&txn->tx_send_txn, rmap->scratch, packet_length, 0 /* no timestamp needed */);
}

// returns true if packet is a valid reply, and false otherwise.
static bool rmap_validate_write_reply(rmap_t *rmap, uint8_t *in, size_t count, uint8_t *status_byte_out) {
    assert(rmap != NULL && in != NULL && status_byte_out != NULL);
    // validate basic parameters of a valid RMAP packet
    if (count < 8) {
        debugf(WARNING, "RMAP (%10s) dropped truncated packet (len=%zu).", rmap->label, count);
        return false;
    }
    if (in[1] != PROTOCOL_RMAP) {
        debugf(WARNING, "RMAP (%10s) dropped packet with wrong protocol (len=%zu, proto=%u).",
               rmap->label, count, in[1]);
        return false;
    }
    // validate that this is the correct type of RMAP packet
    uint8_t flags = in[2];
    if ((flags & (RF_RESERVED | RF_COMMAND | RF_WRITE | RF_VERIFY | RF_ACKNOWLEDGE | RF_INCREMENT))
                != (RF_WRITE | RF_VERIFY | RF_ACKNOWLEDGE | RF_INCREMENT)) {
        debugf(WARNING, "RMAP (%10s) dropped packet (len=%zu) with incorrect flags 0x%02x when pending write.",
               rmap->label, count, flags);
        return false;
    }
    // validate header integrity (length, CRC)
    if (count != 8) {
        debugf(WARNING, "RMAP (%10s) dropped packet exceeding RMAP write reply length (len=%zu).", rmap->label, count);
        return false;
    }
    uint8_t computed_crc = rmap_crc8(in, 7);
    if (computed_crc != in[7]) {
        debugf(WARNING, "RMAP (%10s) dropped write reply with invalid CRC (found=0x%02x, expected=0x%02x).",
               rmap->label, computed_crc, in[7]);
        return false;
    }
    // verify transaction ID
    uint16_t txn_id = (in[5] << 8) | in[6];
    if (txn_id != rmap->current_txn_id) {
        debugf(WARNING, "RMAP (%10s) dropped write reply with wrong transaction ID (found=0x%04x, expected=0x%04x).",
               rmap->label, txn_id, rmap->current_txn_id);
        return false;
    }
    // make sure routing addresses match
    if (in[0] != rmap->routing->source.logical_address || in[4] != rmap->routing->destination.logical_address) {
        debugf(WARNING, "RMAP (%10s) dropped write reply with invalid addressing (%u <- %u but expected %u <- %u).",
               rmap->label, in[0], in[4],
               rmap->routing->source.logical_address, rmap->routing->destination.logical_address);
        return false;
    }
    *status_byte_out = in[3];
    return true;
}

// this should be called one epoch later, to give the networking infrastructure time to respond
rmap_status_t rmap_write_complete(rmap_txn_t *txn, uint64_t *ack_timestamp_out) {
    assert(txn != NULL);
    rmap_t *rmap = txn->rmap;
    assert(rmap != NULL);

    uint8_t status_byte;
    uint64_t timestamp = 0;
    size_t packet_length = duct_receive_message(&txn->rx_recv_txn, rmap->scratch, &timestamp);
    if (packet_length == 0 || !rmap_validate_write_reply(rmap, rmap->scratch, packet_length, &status_byte)) {
        // no need to check for further packets... our duct only allows one packet per epoch!
#ifdef RMAP_TRACE
        debugf(TRACE, "RMAP (%10s) WRITE  FAIL: NO RESPONSE", rmap->label);
#endif
        return RS_NO_RESPONSE;
    }

    if (ack_timestamp_out) {
        *ack_timestamp_out = timestamp;
    }

#ifdef RMAP_TRACE
    debugf(TRACE, "RMAP (%10s) WRITE  DONE: STATUS=%u", rmap->label, status_byte);
#endif

    return status_byte;
}

void rmap_read_start(rmap_txn_t *txn, uint8_t ext_addr, uint32_t main_addr, size_t data_length) {
    assert(txn != NULL);
    rmap_t *rmap = txn->rmap;
    assert(rmap != NULL);
    assert(data_length <= duct_message_size(rmap->rx_duct) - SCRATCH_MARGIN_READ);

#ifdef RMAP_TRACE
    debugf(TRACE, "RMAP (%10s)  READ START: ADDR=0x%02x_%08x LEN=0x%zx",
           rmap->label, ext_addr, main_addr, data_length);
#endif

    if (!duct_send_allowed(&txn->tx_send_txn)) {
        abortf("RMAP (%10s) not permitted to transmit another packet during this epoch.", rmap->label);
    }

    uint8_t *out = rmap->scratch;
    memset(out, 0, duct_message_size(rmap->tx_duct));

    // start writing output bytes according to the read command format
    if (rmap->routing->destination.num_path_bytes > 0) {
        assert(rmap->routing->destination.num_path_bytes <= RMAP_MAX_PATH);
        assert(rmap->routing->destination.path_bytes != NULL);
        memcpy(out, rmap->routing->destination.path_bytes, rmap->routing->destination.num_path_bytes);
        out += rmap->routing->destination.num_path_bytes;
    }
    uint8_t *header_region = out;
    *out++ = rmap->routing->destination.logical_address;
    *out++ = PROTOCOL_RMAP;
    int spal = (rmap->routing->source.num_path_bytes + 3) / 4;
    assert((spal & RF_SOURCEPATH) == spal);
    *out++ = RF_COMMAND | RF_ACKNOWLEDGE | RF_INCREMENT | spal;
    *out++ = rmap->routing->dest_key;
    rmap_encode_source_path(&out, &rmap->routing->source);
    *out++ = rmap->routing->source.logical_address;

    rmap->current_txn_id += 1;

    *out++ = (rmap->current_txn_id >> 8) & 0xFF;
    *out++ = (rmap->current_txn_id >> 0) & 0xFF;
    *out++ = ext_addr;
    *out++ = (main_addr >> 24) & 0xFF;
    *out++ = (main_addr >> 16) & 0xFF;
    *out++ = (main_addr >> 8) & 0xFF;
    *out++ = (main_addr >> 0) & 0xFF;
    assert((data_length >> 24) == 0); // should already be guaranteed by previous checks, but just in case
    *out++ = (data_length >> 16) & 0xFF;
    *out++ = (data_length >> 8) & 0xFF;
    *out++ = (data_length >> 0) & 0xFF;

    // compute the header CRC
    uint8_t header_crc = rmap_crc8(header_region, out - header_region);
    *out++ = header_crc;

    size_t packet_length = out - rmap->scratch;
    assert(packet_length <= duct_message_size(rmap->tx_duct));
    duct_send_message(&txn->tx_send_txn, rmap->scratch, packet_length, 0 /* no timestamp needed */);
}

// returns true if packet is a valid reply, and false otherwise.
static bool rmap_validate_read_reply(rmap_t *rmap, uint8_t *in, size_t count,
                                     uint8_t *status_byte_out, uint8_t *packet_out, size_t *packet_length_io) {
    assert(rmap != NULL && in != NULL && status_byte_out != NULL && packet_out != NULL && packet_length_io != NULL);
    // validate basic parameters of a valid RMAP packet
    if (count < 8) {
        debugf(WARNING, "RMAP (%10s) dropped truncated packet (len=%zu).", rmap->label, count);
        return false;
    }
    if (in[1] != PROTOCOL_RMAP) {
        debugf(WARNING, "RMAP (%10s) dropped non-RMAP packet (len=%zu, proto=%u).", rmap->label, count, in[1]);
        return false;
    }
    // validate that this is the correct type of RMAP packet
    uint8_t flags = in[2];
    if ((flags & (RF_RESERVED | RF_COMMAND | RF_ACKNOWLEDGE | RF_INCREMENT)) != (RF_ACKNOWLEDGE | RF_INCREMENT)) {
        debugf(WARNING, "RMAP (%10s) dropped packet (len=%zu) with incorrect flags 0x%02x when pending read.",
               rmap->label, count, flags);
        return false;
    }
    // validate header integrity (length, CRC)
    if (count < 13) {
        debugf(WARNING, "RMAP (%10s) dropped truncated RMAP read reply packet (len=%zu).", rmap->label, count);
        return false;
    }
    uint8_t computed_crc = rmap_crc8(in, 11);
    if (computed_crc != in[11]) {
        debugf(WARNING, "RMAP (%10s) dropped read reply with invalid header CRC (found=0x%02x, expected=0x%02x).",
               rmap->label, computed_crc, in[11]);
        return false;
    }
    if (in[7] != 0) {
        debugf(WARNING, "RMAP (%10s) dropped invalid read reply with nonzero reserved byte (%u).", rmap->label, in[7]);
        return false;
    }
    // second, validate full length and data CRC after parsing data length.
    uint32_t data_length = (in[8] << 16) | (in[9] << 8) | in[10];
    if (count != 13 + data_length) {
        debugf(WARNING, "RMAP (%10s) dropped read reply with mismatched data length field (found=%u, expected=%zu).",
               rmap->label, data_length, count - 13);
        return false;
    }
    uint8_t *data_ptr = &in[12];
    uint8_t data_crc = rmap_crc8(data_ptr, data_length);
    if (data_crc != in[count - 1]) {
        debugf(WARNING, "RMAP (%10s) dropped read reply with invalid data CRC (found=0x%02x, expected=0x%02x).",
               rmap->label, data_crc, in[count - 1]);
        return false;
    }
    // verify transaction ID
    uint16_t txn_id = (in[5] << 8) | in[6];
    if (txn_id != rmap->current_txn_id) {
        debugf(WARNING, "RMAP (%10s) dropped read reply with wrong transaction ID (found=0x%04x, expected=0x%04x).",
               rmap->label, txn_id, rmap->current_txn_id);
        return false;
    }
    // make sure routing addresses match
    if (in[0] != rmap->routing->source.logical_address || in[4] != rmap->routing->destination.logical_address) {
        debugf(WARNING, "RMAP (%10s) dropped write reply with invalid addressing (%u <- %u but expected %u <- %u).",
               rmap->label, in[0], in[4],
               rmap->routing->source.logical_address, rmap->routing->destination.logical_address);
        return false;
    }
    *status_byte_out = in[3];
    size_t max_len = *packet_length_io;
    if (data_length < max_len) {
        max_len = data_length;
    }
    *packet_length_io = data_length;
    memcpy(packet_out, data_ptr, max_len);
    return true;
}

rmap_status_t rmap_read_complete(rmap_txn_t *txn, uint8_t *buffer, size_t buffer_size, uint64_t *ack_timestamp_out) {
    assert(txn != NULL);
    rmap_t *rmap = txn->rmap;
    assert(rmap != NULL && buffer != NULL);

    uint8_t status_byte;
    size_t output_length = buffer_size;
    uint64_t timestamp = 0;
    size_t packet_length = duct_receive_message(&txn->rx_recv_txn, rmap->scratch, &timestamp);
    if (packet_length == 0 || !rmap_validate_read_reply(rmap, rmap->scratch, packet_length,
                                                        &status_byte, buffer, &output_length)) {
        // no need to check for further packets... our duct only allows one packet per epoch!
#ifdef RMAP_TRACE
        debugf(TRACE, "RMAP (%10s)  READ  FAIL: NO RESPONSE", rmap->label);
#endif
        return RS_NO_RESPONSE;
    }
    if (ack_timestamp_out) {
        *ack_timestamp_out = timestamp;
    }
    if (status_byte != RS_OK) {
#ifdef RMAP_TRACE
        debugf(TRACE, "RMAP (%10s)  READ  FAIL: STATUS=%u", rmap->label, status_byte);
#endif
        return status_byte;
    } else if (output_length != buffer_size) {
#ifdef RMAP_TRACE
        debugf(TRACE, "RMAP (%10s)  READ  FAIL: READ LENGTH DIFFERS: %zu (EXPECTED) != %zu (RECEIVED)",
               rmap->label, buffer_size, output_length);
#endif
        return RS_READ_LENGTH_DIFFERS;
    } else {
#ifdef RMAP_TRACE
        debugf(TRACE, "RMAP (%10s)  READ  DONE: STATUS=OK", rmap->label);
#endif
        return RS_OK;
    }
}
