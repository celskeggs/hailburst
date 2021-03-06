#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <hal/clip.h>
#include <hal/debug.h>
#include <hal/init.h>
#include <hal/timer.h>
#include <synch/strict.h>
#include <bus/exchange.h>

//#define EXCHANGE_DEBUG

#define debug_printf(lvl, fmt, ...) debugf(lvl, "[%s] " fmt, conf->label, ## __VA_ARGS__)

static void rand_init(void) {
    // this does have to be deterministic for our simulations...
    srand(1552);
}
PROGRAM_INIT(STAGE_RAW, rand_init);

void fakewire_exc_rand_clip(duct_t *rand_duct) {
    assert(rand_duct != NULL);

    uint32_t random_number = rand();

    duct_txn_t txn;
    duct_send_prepare(&txn, rand_duct, 0);
    duct_send_message(&txn, &random_number, sizeof(random_number), 0);
    duct_send_commit(&txn);
}

static uint32_t fakewire_exc_receive_random_number(fw_exchange_t *conf) {
    assert(conf != NULL);

    uint32_t random_number;

    duct_txn_t txn;
    duct_receive_prepare(&txn, conf->rand_duct, conf->exchange_replica_id);
    assert(duct_message_size(conf->rand_duct) == sizeof(random_number));
    size_t length = duct_receive_message(&txn, &random_number, NULL);
    if (length == sizeof(random_number)) {
        // great! we have a random number.
    } else {
        // if no random number is available (such as due to a transient failure), use a default.
        // this is okay because it's only used to randomize handshake timings; worst case is a series of colliding
        // handshakes before the transient failure is repaired.
        random_number = 0x12345678;
        miscomparef("Did not receive random number from randomness task (len=%zu).", length);
    }
    duct_receive_commit(&txn);

    return random_number;
}

// random interval in the range [1 tick, 5 ticks]
static uint32_t exchange_handshake_period_ticks(struct fakewire_exchange_note *exc) {
    assert(exc != NULL);

    return exc->random_number % 4 + 1;
}

static void exchange_instance_configure(struct fakewire_exchange_note *exc, uint32_t countdown_timeout) {
    assert(exc != NULL);

    exc->exc_state  = FW_EXC_CONNECTING;
    exc->recv_state = FW_RECV_LISTENING;

    exc->countdown_timeout = countdown_timeout;

    exc->send_handshake_id = 0;
    exc->recv_handshake_id = 0;

    exc->send_primary_handshake   = false;
    exc->send_secondary_handshake = false;

    exc->fcts_sent   = 0;
    exc->fcts_rcvd   = 0;
    exc->pkts_sent   = 0;
    exc->pkts_rcvd   = 0;
    exc->resend_fcts = false;
    exc->resend_pkts = false;

    exc->read_offset = 0;
    exc->read_timestamp = 0;
    exc->write_needs_error = false;
}

static void exchange_instance_reset(struct fakewire_exchange_note *exc) {
    assert(exc != NULL);
    exchange_instance_configure(exc, exc->countdown_timeout);
}

static void exchange_instance_check_timers(fw_exchange_t *conf, struct fakewire_exchange_note *exc) {
    assert(exc != NULL);

    // check timers now
    if (exc->countdown_timeout > 0) {
        exc->countdown_timeout -= 1;
    }
    if (exc->countdown_timeout == 0) {
        if (exc->exc_state == FW_EXC_OPERATING) {
            // send heartbeats
            exc->resend_fcts = true;
            exc->resend_pkts = true;

            exc->countdown_timeout = exchange_handshake_period_ticks(exc);
        } else if (exc->exc_state == FW_EXC_HANDSHAKING || exc->exc_state == FW_EXC_CONNECTING) {
            // send a fresh handshake
            exc->send_primary_handshake = true;

            exc->countdown_timeout = exchange_handshake_period_ticks(exc);
            debug_printf(DEBUG, "Next handshake scheduled for %u ticks in the future", exc->countdown_timeout);
        }
    }
}

static void exchange_instance_check_invariants(struct fakewire_exchange_note *exc) {
    assert(exc != NULL);

    // check invariants
    assert(exc->exc_state >= FW_EXC_CONNECTING && exc->exc_state <= FW_EXC_OPERATING);
    assertf(exc->pkts_sent <= exc->fcts_rcvd && exc->fcts_rcvd <= exc->pkts_sent + MAX_OUTSTANDING_TOKENS,
            "pkts_sent = %u, fcts_rcvd = %u", exc->pkts_sent, exc->fcts_rcvd);
}

static void exchange_recv_ctrl_char_while_connecting(fw_exchange_t *conf, struct fakewire_exchange_note *exc,
                                                     fw_ctrl_t symbol, uint32_t param) {
    assert(conf != NULL && exc != NULL);

    // error condition: if ANYTHING is hit except a handshake_1
    if (symbol != FWC_HANDSHAKE_1) {
        // There's no point in being loud about this; if we're seeing it, we're ALREADY in a broken
        // state, and continuing to spew messages about how everything is still broken is not helpful.
        debug_printf(TRACE, "Unexpected %s(0x%08x) instead of HANDSHAKE_1(*); resetting.",
                     fakewire_codec_symbol(symbol), param);
        exchange_instance_reset(exc);
        return;
    }

    // received a primary handshake
    debug_printf(DEBUG, "Received a primary handshake with ID=0x%08x.", param);
    exc->recv_handshake_id = param;
    exc->send_secondary_handshake = true;
}

static void exchange_recv_ctrl_char_while_handshaking(fw_exchange_t *conf, struct fakewire_exchange_note *exc,
                                                      fw_ctrl_t symbol, uint32_t param) {
    assert(conf != NULL && exc != NULL);

    // error condition: if ANYTHING is hit except a match handshake_2
    if (symbol != FWC_HANDSHAKE_2 || param != exc->send_handshake_id) {
        debug_printf(WARNING, "Unexpected %s(0x%08x) instead of HANDSHAKE_2(0x%08x); resetting.",
                     fakewire_codec_symbol(symbol), param, exc->send_handshake_id);
        exchange_instance_reset(exc);
        return;
    }

    // received a valid secondary handshake
    debug_printf(DEBUG, "Received secondary handshake with ID=0x%08x; transitioning to operating mode.", param);
    exc->exc_state = FW_EXC_OPERATING;
    exc->send_primary_handshake = false;
    exc->send_secondary_handshake = false;
}

static void exchange_recv_ctrl_char_while_operating(fw_exchange_t *conf, struct fakewire_exchange_note *exc,
                                                    duct_txn_t *send_txn, fw_ctrl_t symbol, uint32_t param,
                                                    local_time_t receive_timestamp) {
    assert(conf != NULL && exc != NULL);

    // TODO: act on any received FWC_HANDSHAKE_1 requests immediately.
    switch (symbol) {
    case FWC_START_PACKET:
        if (exc->fcts_sent <= exc->pkts_rcvd) {
            debug_printf(WARNING, "Received unauthorized start-of-packet (fcts_sent=%u, pkts_rcvd=%u); resetting.",
                         exc->fcts_sent, exc->pkts_rcvd);
            exchange_instance_reset(exc);
            break;
        }
        assert(exc->recv_state == FW_RECV_LISTENING);

        // should always be allowed, because the number of fcts we send are based on the max flow rate
        assert(duct_send_allowed(send_txn));

        // reset receive state and buffer before proceeding
        exc->read_offset = 0;
        // Had to disable this memset because it was too slow to be practical.
        // memset(conf->read_buffer, 0, conf->buffers_length);
        exc->read_timestamp = receive_timestamp;

        exc->recv_state = FW_RECV_RECEIVING;
        exc->pkts_rcvd += 1;
        break;
    case FWC_END_PACKET:
        if (exc->recv_state == FW_RECV_OVERFLOWED) {
            // discard state and get ready for another packet
            exc->recv_state = FW_RECV_LISTENING;
        } else if (exc->recv_state != FW_RECV_RECEIVING) {
            debug_printf(WARNING, "Hit unexpected END_PACKET in receive state %d; resetting.", exc->recv_state);
            exchange_instance_reset(exc);
        } else if (exc->read_offset == 0) {
            debug_printf(WARNING, "Packet of length 0 received; discarding.");
            exc->recv_state = FW_RECV_LISTENING;
        } else {
            // transmit received packet through duct
            duct_send_message(send_txn, conf->read_buffer, exc->read_offset, exc->read_timestamp);
            exc->recv_state = FW_RECV_LISTENING;
        }
        break;
    case FWC_ERROR_PACKET:
        if (exc->recv_state != FW_RECV_OVERFLOWED && exc->recv_state != FW_RECV_RECEIVING) {
            debug_printf(WARNING, "Hit unexpected ERROR_PACKET in receive state %d; resetting.", exc->recv_state);
            exchange_instance_reset(exc);
            break;
        }
        // discard state and get ready for another packet
        exc->recv_state = FW_RECV_LISTENING;
        break;
    case FWC_FLOW_CONTROL:
        if (param < exc->fcts_rcvd) {
            // FCT number should never decrease.
            debug_printf(WARNING, "Received abnormally low FCT(%u) when last count was %u; resetting.",
                         param, exc->fcts_rcvd);
            exchange_instance_reset(exc);
        } else if (param > exc->pkts_sent + MAX_OUTSTANDING_TOKENS) {
            // FCT number should never increase more than allowed.
            debug_printf(WARNING, "Received abnormally high FCT(%u) when maximum was %u and last count was %u; "
                         "resetting.", param, exc->pkts_sent + MAX_OUTSTANDING_TOKENS, exc->fcts_rcvd);
            exchange_instance_reset(exc);
        } else {
            // received FCT; may be able to send more packets!
            exc->fcts_rcvd = param;
        }
        break;
    case FWC_KEEP_ALIVE:
        if (exc->pkts_rcvd != param) {
            debug_printf(WARNING, "KAT mismatch: received %u packets, but supposed to have received %u; resetting.",
                         exc->pkts_rcvd, param);
            exchange_instance_reset(exc);
        }
        break;
    default:
        debug_printf(WARNING, "Unexpected %s(0x%08x) during OPERATING mode; resetting.",
                     fakewire_codec_symbol(symbol), param);
        exchange_instance_reset(exc);
        if (symbol == FWC_HANDSHAKE_1) {
            // special case: process received handshakes immediately
            exchange_recv_ctrl_char_while_connecting(conf, exc, symbol, param);
        }
    }
}

static void exchange_instance_receive_ctrl_char(fw_exchange_t *conf, struct fakewire_exchange_note *exc,
                                                duct_txn_t *send_txn, fw_ctrl_t symbol, uint32_t param,
                                                local_time_t receive_timestamp) {
    assert(conf != NULL && exc != NULL);
    assert(param == 0 || fakewire_is_parametrized(symbol));

#ifdef EXCHANGE_DEBUG
    debug_printf(TRACE, "Received control character: %s(0x%08x).", fakewire_codec_symbol(symbol), param);
#endif

    switch (exc->exc_state) {
    case FW_EXC_CONNECTING:
        exchange_recv_ctrl_char_while_connecting(conf, exc, symbol, param);
        break;
    case FW_EXC_HANDSHAKING:
        exchange_recv_ctrl_char_while_handshaking(conf, exc, symbol, param);
        break;
    case FW_EXC_OPERATING:
        exchange_recv_ctrl_char_while_operating(conf, exc, send_txn, symbol, param, receive_timestamp);
        break;
    default:
        assert(false);
    }
}

static void exchange_instance_receive_data_chars(fw_exchange_t *conf, struct fakewire_exchange_note *exc,
                                                 uint8_t *data_in, size_t data_len) {
    assert(conf != NULL && exc != NULL);

    if (exc->recv_state == FW_RECV_OVERFLOWED) {
        assert(exc->exc_state == FW_EXC_OPERATING);
        assert(data_in == NULL);
        // discard extraneous bytes and do nothing
#ifdef EXCHANGE_DEBUG
        debug_printf(DEBUG, "Discarded an additional %zu regular data bytes.", data_len);
#endif
    } else if (exc->exc_state != FW_EXC_OPERATING || exc->recv_state != FW_RECV_RECEIVING) {
        assert(data_in == NULL);
        debug_printf(WARNING, "Received at least %zu unexpected data characters during state (exc=%d, recv=%d); "
                     "resetting.", data_len, exc->exc_state, exc->recv_state);
        exchange_instance_reset(exc);
    } else if (exc->read_offset >= conf->buffers_length) {
        assert(data_in == NULL);
        debug_printf(WARNING, "Packet exceeded buffer size %zu (at least %zu + %zu bytes); discarding.",
                     conf->buffers_length, exc->read_offset, data_len);
        exc->recv_state = FW_RECV_OVERFLOWED;
    } else {
        assert(data_in != NULL);
        assert(exc->read_offset + data_len <= conf->buffers_length);
#ifdef EXCHANGE_DEBUG
        debug_printf(TRACE, "Received %zu regular data bytes.", data_len);
#endif
        exc->read_offset += data_len;
        assert(exc->read_offset <= conf->buffers_length);
    }
}

static bool exchange_instance_receive(fw_exchange_t *conf, struct fakewire_exchange_note *exc, duct_txn_t *send_txn) {
    assert(conf != NULL && exc != NULL);

    // discard all data and just tell us the number of bytes
    fw_decoded_ent_t rx_ent = {
        .data_out     = NULL,
        .data_max_len = 0,
    };

    // UNLESS we have somewhere we can put that data, in which case put it there
    if (exc->exc_state == FW_EXC_OPERATING && exc->recv_state == FW_RECV_RECEIVING
            && exc->read_offset < conf->buffers_length) {
        rx_ent.data_out     = conf->read_buffer + exc->read_offset;
        rx_ent.data_max_len = conf->buffers_length - exc->read_offset;
    }

    if (!fakewire_dec_decode(conf->decoder, &exc->decoder_synch, &rx_ent)) {
        // no more data to receive right now; wait until next wakeup
        return false;
    }
    // process received control character or data characters
    if (rx_ent.ctrl_out != FWC_NONE) {
        assert(rx_ent.data_actual_len == 0);

        exchange_instance_receive_ctrl_char(conf, exc, send_txn,
                                            rx_ent.ctrl_out, rx_ent.ctrl_param, rx_ent.receive_timestamp);
    } else {
        assert(rx_ent.data_actual_len > 0);

        exchange_instance_receive_data_chars(conf, exc, rx_ent.data_out, rx_ent.data_actual_len);
    }

    return true;
}

static void exchange_instance_check_fcts(fw_exchange_t *conf, struct fakewire_exchange_note *exc) {
    assert(conf != NULL && exc != NULL);

    uint32_t not_yet_received = duct_max_flow(conf->read_duct);
    if (exc->recv_state != FW_RECV_LISTENING) {
        not_yet_received -= 1;
    }
    if (not_yet_received > MAX_OUTSTANDING_TOKENS) {
        not_yet_received = MAX_OUTSTANDING_TOKENS;
    }
    if (exc->exc_state == FW_EXC_OPERATING && exc->pkts_rcvd + not_yet_received > exc->fcts_sent) {
#ifdef EXCHANGE_DEBUG
        debug_printf(TRACE, "Sending FCT.");
#endif
        exc->fcts_sent = exc->pkts_rcvd + not_yet_received;
        exc->resend_fcts = true;
        exc->resend_pkts = true;

        exc->countdown_timeout = exchange_handshake_period_ticks(exc);
    }
}

static void exchange_instance_transmit_tokens(fw_exchange_t *conf, struct fakewire_exchange_note *exc) {
    assert(conf != NULL && exc != NULL);

    if (exc->resend_fcts && fakewire_enc_encode_ctrl(conf->encoder, FWC_FLOW_CONTROL, exc->fcts_sent)) {
        assert(exc->exc_state == FW_EXC_OPERATING);
        exc->resend_fcts = false;
#ifdef EXCHANGE_DEBUG
        debug_printf(TRACE, "Transmitted reminder FCT(%u) tokens.", exc->fcts_sent);
#endif
    }

    if (exc->resend_pkts && fakewire_enc_encode_ctrl(conf->encoder, FWC_KEEP_ALIVE, exc->pkts_sent)) {
        assert(exc->exc_state == FW_EXC_OPERATING);
        exc->resend_pkts = false;
#ifdef EXCHANGE_DEBUG
        debug_printf(TRACE, "Transmitted reminder KAT(%u) tokens.", exc->pkts_sent);
#endif
    }
}

static void exchange_instance_transmit_handshakes(fw_exchange_t *conf, struct fakewire_exchange_note *exc) {
    assert(conf != NULL && exc != NULL);

    if (exc->send_secondary_handshake
            && fakewire_enc_encode_ctrl(conf->encoder, FWC_HANDSHAKE_2, exc->recv_handshake_id)) {
        assert(exc->exc_state == FW_EXC_CONNECTING);

        exc->exc_state = FW_EXC_OPERATING;
        exc->send_primary_handshake = false;
        exc->send_secondary_handshake = false;

        debug_printf(DEBUG, "Sent secondary handshake with ID=0x%08x; transitioning to operating mode.",
                     exc->recv_handshake_id);

        exc->countdown_timeout = exchange_handshake_period_ticks(exc);
    }

    if (exc->send_primary_handshake) {
        assert(exc->exc_state == FW_EXC_HANDSHAKING || exc->exc_state == FW_EXC_CONNECTING);

        // pick something very likely to be distinct (Go picks msb unset, C picks msb set)
        uint32_t gen_handshake_id = 0x80000000 + (0x7FFFFFFF & exc->random_number);

        if (fakewire_enc_encode_ctrl(conf->encoder, FWC_HANDSHAKE_1, gen_handshake_id)) {
            exc->send_handshake_id = gen_handshake_id;

            exc->exc_state = FW_EXC_HANDSHAKING;
            exc->send_primary_handshake = false;
            exc->send_secondary_handshake = false;

            debug_printf(DEBUG, "Sent primary handshake with ID=0x%08x; transitioning to handshaking mode.",
                         gen_handshake_id);
        }
    }
}

static bool exchange_instance_transmit_data(fw_exchange_t *conf, struct fakewire_exchange_note *exc, size_t length) {
    assert(conf != NULL && exc != NULL);

    if (exc->exc_state != FW_EXC_OPERATING) {
        // can't transmit anything until we're in the operating state. drop packets instead.
        return false;
    }

    if (exc->write_needs_error) {
        // if we weren't able to transmit the whole last packet, then we need to make sure to transmit ERROR_PACKET to
        // make sure the remote end drops it instead of trying to process it.
        if (fakewire_enc_encode_ctrl(conf->encoder, FWC_ERROR_PACKET, 0)) {
            exc->write_needs_error = false;
        } else {
            debugf(TRACE, "Transmit buffer is full.");
            return false;
        }
    }

    if (exc->pkts_sent >= exc->fcts_rcvd) {
        // no flow control tokens received; can't transmit any packets yet. drop them instead.
        debugf(TRACE, "No more flow control tokens available.");
        return false;
    }
    if (!fakewire_enc_encode_ctrl(conf->encoder, FWC_START_PACKET, 0)) {
        // no room to write START_PACKET; drop the packet and try again next epoch.
        debugf(TRACE, "Transmit buffer is full.");
        return false;
    }

    // sent a START_PACKET, so increment pkts_sent.
    exc->pkts_sent++;

    size_t actually_written = fakewire_enc_encode_data(conf->encoder, conf->write_buffer, length);
    if (actually_written < length) {
        // not enough room to finish writing the whole packet at once; drop it.
        exc->write_needs_error = true;
        debugf(TRACE, "Transmit buffer is either full or not large enough.");
        return false;
    }

    if (!fakewire_enc_encode_ctrl(conf->encoder, FWC_END_PACKET, 0)) {
        // no room to write END_PACKET; drop it. (disappointing, but the alternative is transmitting a packet late.)
        exc->write_needs_error = true;
        debugf(TRACE, "Transmit buffer is full.");
        return false;
    }

#ifdef EXCHANGE_DEBUG
    debug_printf(TRACE, "Transmitted packet (len=%u).", length);
#endif

    return true;
}

static struct fakewire_exchange_note *fakewire_exc_feedforward(fw_exchange_t *conf) {
    assert(conf != NULL);
    bool valid = false;
    struct fakewire_exchange_note *exc = notepad_feedforward(conf->mut_synch, &valid);
    assert(exc != NULL);

    uint32_t random_number = fakewire_exc_receive_random_number(conf);

    if (!valid) {
        memset(exc, 0, sizeof(*exc));
        exc->random_number = random_number;
        exchange_instance_configure(exc, exchange_handshake_period_ticks(exc));
        fakewire_dec_reset(conf->decoder, &exc->decoder_synch);

        debug_printf(DEBUG, "First handshake scheduled for %u ticks in the future", exc->countdown_timeout);
    } else {
        exc->random_number = random_number;
    }

    return exc;
}

void fakewire_exc_tx_clip(fw_exchange_t *conf) {
    assert(conf != NULL);
    struct fakewire_exchange_note *exc = fakewire_exc_feedforward(conf);
    assert(exc != NULL);

    exchange_instance_check_invariants(exc);

    duct_txn_t recv_txn;
    duct_receive_prepare(&recv_txn, conf->write_duct, conf->exchange_replica_id);
    fakewire_enc_prepare(conf->encoder);

    exchange_instance_transmit_tokens(conf, exc);

    exchange_instance_transmit_handshakes(conf, exc);

    duct_flow_index dropped = 0;
    size_t packet_length;
    assert(conf->buffers_length == duct_message_size(conf->write_duct));
    while (0 != (packet_length = duct_receive_message(&recv_txn, conf->write_buffer, NULL))) {
        assert(0 < packet_length && packet_length <= conf->buffers_length);
        if (!exchange_instance_transmit_data(conf, exc, packet_length)) {
            dropped++;
        }
    }
    if (dropped) {
        debug_printf(WARNING, "Dropped %u packets blocked from transmission.", dropped);
    }

    duct_receive_commit(&recv_txn);
    fakewire_enc_commit(conf->encoder);
}

void fakewire_exc_rx_clip(fw_exchange_t *conf) {
    assert(conf != NULL);
    struct fakewire_exchange_note *exc = fakewire_exc_feedforward(conf);
    assert(exc != NULL);

    exchange_instance_check_invariants(exc);

    exchange_instance_check_timers(conf, exc);

    duct_txn_t send_txn;
    duct_send_prepare(&send_txn, conf->read_duct, conf->exchange_replica_id);
    fakewire_dec_prepare(conf->decoder);
    // keep receiving data up to the processing limit, which should be plenty for ordinary situations. if we exceed
    // this limit, then we're probably catching up after a reset, and we don't want to keep going, because then we'll
    // run out of time. just dump everything else and try again.
    duct_flow_index receive_limit = duct_max_flow(conf->read_duct) * 2;
    duct_flow_index remaining_limit = receive_limit;
    while (exchange_instance_receive(conf, exc, &send_txn)) {
        if (--remaining_limit == 0) {
            size_t remaining = fakewire_dec_remaining_bytes(conf->decoder);
            if (remaining > 0) {
                debug_printf(WARNING, "Tossing remaining %zu received bytes due to overflow of receive limit (%u); "
                             "resetting.", remaining, receive_limit);
                fakewire_dec_reset(conf->decoder, &exc->decoder_synch);
                exchange_instance_reset(exc);
            }
        }
    }
    duct_send_commit(&send_txn);
    fakewire_dec_commit(conf->decoder);

    exchange_instance_check_fcts(conf, exc);
}
