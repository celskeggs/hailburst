#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <hal/clock.h>
#include <hal/debug.h>
#include <hal/init.h>
#include <hal/thread.h>
#include <bus/exchange.h>

//#define EXCHANGE_DEBUG

#define debug_printf(lvl, fmt, ...) debugf(lvl, "[%s] " fmt, exc->conf->label, ## __VA_ARGS__)

static void rand_init(void) {
    // this does have to be deterministic for our simulations...
    srand(1552);
}
PROGRAM_INIT(STAGE_RAW, rand_init);

// random interval in the range [3ms, 10ms)
static uint64_t handshake_period(void) {
    uint64_t ms = 1000 * 1000;
    return (rand() % (7 * ms)) + 3 * ms;
}

static void exchange_instance_configure(exchange_instance_t *exc, fw_exchange_t *conf, uint64_t next_timeout) {
    assert(exc != NULL && conf != NULL);

    exc->conf = conf;

    exc->exc_state    = FW_EXC_CONNECTING;
    exc->recv_state   = FW_RECV_LISTENING;
    exc->txmit_state  = FW_TXMIT_IDLE;

    exc->next_timeout = next_timeout;

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

    exc->read_entry   = NULL;
    exc->write_entry  = NULL;
    exc->write_offset = 0;
}

static void exchange_instance_reset(exchange_instance_t *exc) {
    assert(exc != NULL);
    exchange_instance_configure(exc, exc->conf, exc->next_timeout);
}

static void exchange_instance_doze(exchange_instance_t *exc) {
    assert(exc != NULL);

    // if we've gotten a notification already, don't bother figuring out a specific way to doze
    if (task_doze_try()) {
        return;
    }

    // flush encoder before we sleep
    fakewire_enc_flush(&exc->encoder);

    if (exc->exc_state == FW_EXC_OPERATING && (!exc->resend_fcts || !exc->resend_pkts)) {
        // do a timed wait, so that we can send heartbeats when it's an appropriate time
        if (!task_doze_timed_abs(exc->next_timeout)) {
            assert(clock_timestamp_monotonic() >= exc->next_timeout);

            exc->resend_fcts = true;
            exc->resend_pkts = true;

            exc->next_timeout = clock_timestamp_monotonic() + handshake_period();
        }
    } else if ((exc->exc_state == FW_EXC_HANDSHAKING || exc->exc_state == FW_EXC_CONNECTING)
                    && !exc->send_primary_handshake) {
        // do a timed wait, so that we can send a fresh handshake when it's an appropriate time
        if (!task_doze_timed_abs(exc->next_timeout)) {
            assert(clock_timestamp_monotonic() >= exc->next_timeout);

            exc->send_primary_handshake = true;

            exc->next_timeout = clock_timestamp_monotonic() + handshake_period();
            debug_printf(DEBUG, "Next handshake scheduled for " TIMEFMT, TIMEARG(exc->next_timeout));
        }
    } else {
        task_doze();
    }
}

static void exchange_instance_check_invariants(exchange_instance_t *exc) {
    assert(exc != NULL);

    // check invariants
    assert(exc->exc_state >= FW_EXC_CONNECTING && exc->exc_state <= FW_EXC_OPERATING);
    assertf(exc->pkts_sent <= exc->fcts_rcvd && exc->fcts_rcvd <= exc->pkts_sent + MAX_OUTSTANDING_TOKENS,
            "pkts_sent = %u, fcts_rcvd = %u", exc->pkts_sent, exc->fcts_rcvd);
}

static void exchange_recv_ctrl_char_while_connecting(exchange_instance_t *exc, fw_ctrl_t symbol, uint32_t param) {
    assert(exc != NULL);

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

static void exchange_recv_ctrl_char_while_handshaking(exchange_instance_t *exc, fw_ctrl_t symbol, uint32_t param) {
    assert(exc != NULL);

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

static void exchange_recv_ctrl_char_while_operating(exchange_instance_t *exc, fw_ctrl_t symbol, uint32_t param,
                                                    uint64_t receive_timestamp) {
    assert(exc != NULL);

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

        assert(exc->read_entry == NULL);
        exc->read_entry = chart_request_start(exc->conf->read_chart);
        assert(exc->read_entry != NULL);
        exc->read_entry->actual_length = 0;
        exc->read_entry->receive_timestamp = receive_timestamp;

        exc->recv_state = FW_RECV_RECEIVING;
        exc->pkts_rcvd += 1;
        // reset receive buffer before proceeding
        memset(exc->read_entry->data, 0, io_rx_size(exc->conf->read_chart));
        break;
    case FWC_END_PACKET:
        if (exc->recv_state == FW_RECV_OVERFLOWED) {
            // discard state and get ready for another packet
            exc->recv_state = FW_RECV_LISTENING;
            exc->read_entry = NULL;
        } else if (exc->recv_state == FW_RECV_RECEIVING) {
            assert(exc->read_entry != NULL);
            // notify read task that data is ready to consume
            chart_request_send(exc->conf->read_chart, 1);
            exc->recv_state = FW_RECV_LISTENING;
            exc->read_entry = NULL;
        } else {
            debug_printf(WARNING, "Hit unexpected END_PACKET in receive state %d; resetting.", exc->recv_state);
            exchange_instance_reset(exc);
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
        exc->read_entry = NULL;
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
    }
}

static void exchange_instance_receive_ctrl_char(exchange_instance_t *exc, fw_ctrl_t symbol, uint32_t param,
                                                uint64_t receive_timestamp) {
    assert(exc != NULL);
    assert(param == 0 || fakewire_is_parametrized(symbol));

#ifdef EXCHANGE_DEBUG
    debug_printf(TRACE, "Received control character: %s(0x%08x).", fakewire_codec_symbol(symbol), param);
#endif

    switch (exc->exc_state) {
    case FW_EXC_CONNECTING:
        exchange_recv_ctrl_char_while_connecting(exc, symbol, param);
        break;
    case FW_EXC_HANDSHAKING:
        exchange_recv_ctrl_char_while_handshaking(exc, symbol, param);
        break;
    case FW_EXC_OPERATING:
        exchange_recv_ctrl_char_while_operating(exc, symbol, param, receive_timestamp);
        break;
    default:
        assert(false);
    }
}

static void exchange_instance_receive_data_chars(exchange_instance_t *exc, uint8_t *data_in, size_t data_len) {
    assert(exc != NULL);

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
    } else if (exc->read_entry->actual_length >= io_rx_size(exc->conf->read_chart)) {
        assert(data_in == NULL);
        debug_printf(WARNING, "Packet exceeded buffer size %u (at least %u + %zu bytes); discarding.",
                     io_rx_size(exc->conf->read_chart), exc->read_entry->actual_length, data_len);
        exc->recv_state = FW_RECV_OVERFLOWED;
    } else {
        assert(data_in != NULL);
        assert(exc->read_entry->actual_length + data_len <= io_rx_size(exc->conf->read_chart));
#ifdef EXCHANGE_DEBUG
        debug_printf(TRACE, "Received %zu regular data bytes.", data_len);
#endif
        exc->read_entry->actual_length += data_len;
        assert(exc->read_entry->actual_length <= io_rx_size(exc->conf->read_chart));
    }
}

static bool exchange_instance_receive(exchange_instance_t *exc) {
    // discard all data and just tell us the number of bytes
    fw_decoded_ent_t rx_ent = {
        .data_out     = NULL,
        .data_max_len = 0,
    };

    // UNLESS we have somewhere we can put that data, in which case put it there
    if (exc->exc_state == FW_EXC_OPERATING && exc->recv_state == FW_RECV_RECEIVING
            && exc->read_entry->actual_length < io_rx_size(exc->conf->read_chart)) {
        rx_ent.data_out     = exc->read_entry->data + exc->read_entry->actual_length;
        rx_ent.data_max_len = io_rx_size(exc->conf->read_chart) - exc->read_entry->actual_length;
    }

    if (!fakewire_dec_decode(&exc->decoder, &rx_ent)) {
        // no more data to receive right now; wait until next wakeup
        return false;
    }
    // process received control character or data characters
    if (rx_ent.ctrl_out != FWC_NONE) {
        assert(rx_ent.data_actual_len == 0);

        exchange_instance_receive_ctrl_char(exc, rx_ent.ctrl_out, rx_ent.ctrl_param, rx_ent.receive_timestamp);
    } else {
        assert(rx_ent.data_actual_len > 0);

        exchange_instance_receive_data_chars(exc, rx_ent.data_out, rx_ent.data_actual_len);
    }

    return true;
}

static void exchange_instance_check_fcts(exchange_instance_t *exc) {
    assert(exc != NULL);

    uint32_t not_yet_received = chart_request_avail(exc->conf->read_chart);
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

        exc->next_timeout = clock_timestamp_monotonic() + handshake_period();
    }
}

static void exchange_instance_transmit_tokens(exchange_instance_t *exc) {
    assert(exc != NULL);

    if (exc->resend_fcts && fakewire_enc_encode_ctrl(&exc->encoder, FWC_FLOW_CONTROL, exc->fcts_sent)) {
        assert(exc->exc_state == FW_EXC_OPERATING);
        exc->resend_fcts = false;
#ifdef EXCHANGE_DEBUG
        debug_printf(TRACE, "Transmitted reminder FCT(%u) tokens.", exc->fcts_sent);
#endif
    }

    if (exc->resend_pkts && fakewire_enc_encode_ctrl(&exc->encoder, FWC_KEEP_ALIVE, exc->pkts_sent)) {
        assert(exc->exc_state == FW_EXC_OPERATING);
        exc->resend_pkts = false;
#ifdef EXCHANGE_DEBUG
        debug_printf(TRACE, "Transmitted reminder KAT(%u) tokens.", exc->pkts_sent);
#endif
    }
}

static void exchange_instance_transmit_handshakes(exchange_instance_t *exc) {
    assert(exc != NULL);

    if (exc->send_primary_handshake) {
        assert(exc->exc_state == FW_EXC_HANDSHAKING || exc->exc_state == FW_EXC_CONNECTING);

        // pick something very likely to be distinct (Go picks msb unset, C picks msb set)
        uint32_t gen_handshake_id = 0x80000000 + (0x7FFFFFFF & (uint32_t) clock_timestamp_monotonic());

        if (fakewire_enc_encode_ctrl(&exc->encoder, FWC_HANDSHAKE_1, gen_handshake_id)) {
            exc->send_handshake_id = gen_handshake_id;

            exc->exc_state = FW_EXC_HANDSHAKING;
            exc->send_primary_handshake = false;
            exc->send_secondary_handshake = false;

            debug_printf(DEBUG, "Sent primary handshake with ID=0x%08x; transitioning to handshaking mode.",
                         gen_handshake_id);
        }
    }

    if (exc->send_secondary_handshake
            && fakewire_enc_encode_ctrl(&exc->encoder, FWC_HANDSHAKE_2, exc->recv_handshake_id)) {
        assert(exc->exc_state == FW_EXC_CONNECTING);

        exc->exc_state = FW_EXC_OPERATING;
        exc->send_primary_handshake = false;
        exc->send_secondary_handshake = false;

        debug_printf(DEBUG, "Sent secondary handshake with ID=0x%08x; transitioning to operating mode.",
                     exc->recv_handshake_id);

        exc->next_timeout = clock_timestamp_monotonic() + handshake_period();
    }
}

static bool exchange_instance_transmit_data(exchange_instance_t *exc) {
    assert(exc != NULL);

    if (exc->txmit_state == FW_TXMIT_IDLE) {
        assert(exc->write_entry == NULL);
        exc->write_entry = chart_reply_start(exc->conf->write_chart);
        if (exc->write_entry == NULL) {
            // no more write requests remaining
            return false;
        }

        assert(exc->write_entry->actual_length > 0);
#ifdef EXCHANGE_DEBUG
        debug_printf(TRACE, "Received packet (len=%u) to transmit.", exc->write_entry->actual_length);
#endif
        exc->write_offset = 0;
        exc->txmit_state = FW_TXMIT_HEADER;
    }

    if (exc->exc_state == FW_EXC_OPERATING && exc->txmit_state == FW_TXMIT_HEADER && exc->pkts_sent < exc->fcts_rcvd
            && fakewire_enc_encode_ctrl(&exc->encoder, FWC_START_PACKET, 0)) {
        assert(exc->write_entry != NULL && exc->write_offset == 0);

        exc->txmit_state = FW_TXMIT_BODY;
        exc->pkts_sent++;
    }

    if (exc->exc_state == FW_EXC_OPERATING && exc->txmit_state == FW_TXMIT_BODY) {
        assert(exc->write_entry != NULL && exc->write_offset < exc->write_entry->actual_length);

        size_t actually_written = fakewire_enc_encode_data(&exc->encoder, exc->write_entry->data + exc->write_offset,
                                                           exc->write_entry->actual_length - exc->write_offset);
        if (actually_written + exc->write_offset == exc->write_entry->actual_length) {
            exc->txmit_state = FW_TXMIT_FOOTER;
        } else {
            assert(actually_written < exc->write_entry->actual_length - exc->write_offset);
            exc->write_offset += actually_written;
        }
    }

    if (exc->exc_state == FW_EXC_OPERATING && exc->txmit_state == FW_TXMIT_FOOTER
            && fakewire_enc_encode_ctrl(&exc->encoder, FWC_END_PACKET, 0)) {
        assert(exc->write_entry != NULL);

        // respond to writer
#ifdef EXCHANGE_DEBUG
        debug_printf(TRACE, "Finished transmitting packet (len=%u).", exc->write_entry->actual_length);
#endif
        chart_reply_send(exc->conf->write_chart, 1);

        // reset our state
        exc->txmit_state = FW_TXMIT_IDLE;
        exc->write_entry = NULL;
        exc->write_offset = 0;
    }

    // if we didn't get to idle, stop; we can't make any more progress right now.
    return exc->txmit_state == FW_TXMIT_IDLE;
}

void fakewire_exc_exchange_loop(const fw_exchange_t *conf) {
    assert(conf != NULL);
    exchange_instance_t *exc = conf->instance;
    assert(exc != NULL);

    uint64_t first_timeout = clock_timestamp_monotonic() + handshake_period();

    memset(exc, 0, sizeof(*exc));
    exchange_instance_configure(exc, conf, first_timeout);
    fakewire_enc_init(&exc->encoder, conf->transmit_chart);
    fakewire_dec_init(&exc->decoder, conf->receive_chart);

    debug_printf(DEBUG, "First handshake scheduled for " TIMEFMT, TIMEARG(first_timeout));

    while (true) {
        exchange_instance_doze(exc);

        exchange_instance_check_invariants(exc);

        // keep receiving line data as long as there's more data to receive; we don't want to sleep until there's
        // nothing left, so that we can guarantee a wakeup will still be pending afterwards,
        while (exchange_instance_receive(exc)) {
            // keep looping
        }

        exchange_instance_check_fcts(exc);

        exchange_instance_transmit_tokens(exc);

        exchange_instance_transmit_handshakes(exc);

        // we want to keep trying to transmit until we either a) run out of pending write requests, or b) run out of
        // encoding buffer space to write those requests. That way, we can be guaranteed that there will be a wakeup
        // pending if there's anything more to do.
        while (exchange_instance_transmit_data(exc)) {
            // keep looping
        }
    }
}
