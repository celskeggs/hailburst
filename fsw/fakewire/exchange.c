#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <hal/thread.h>
#include <fsw/clock.h>
#include <fsw/debug.h>
#include <fsw/fakewire/exchange.h>

//#define DEBUG
//#define APIDEBUG

#define debug_printf(lvl, fmt, ...) debugf(lvl, "[%s] " fmt, fwe->label, ## __VA_ARGS__)

void fakewire_exc_notify(void *opaque) {
    fw_exchange_t *fwe = (fw_exchange_t *) opaque;
    assert(fwe != NULL);

    // we don't care if our give actually succeeds... if it doesn't, that just means there's already a wakeup pending,
    // and in that case it'll be woken up anyway!
    (void) semaphore_give(&fwe->exchange_wake);
}

void fakewire_exc_init_internal(fw_exchange_t *fwe) {
    assert(fwe != NULL);

    semaphore_init(&fwe->exchange_wake);

    fakewire_enc_init(&fwe->encoder, fwe->transmit_chart);
    fakewire_dec_init(&fwe->decoder, fwe->receive_chart);
}

// custom exchange protocol
enum exchange_state {
    FW_EXC_INVALID = 0, // should never be set to this value during normal execution
    FW_EXC_CONNECTING,  // waiting for primary handshake, or, if none received, will send primary handshake
    FW_EXC_HANDSHAKING, // waiting for secondary handshake, or, if primary received, will reset
    FW_EXC_OPERATING,   // received a valid non-conflicting handshake
};

enum receive_state {
    FW_RECV_PREPARING = 0, // waiting for operating mode to be reached and for FCT to be sent
    FW_RECV_LISTENING,     // waiting for Start-of-Packet character
    FW_RECV_RECEIVING,     // receiving data body of packet
    FW_RECV_OVERFLOWED,    // received data too large for buffer; waiting for end before discarding
};

enum transmit_state {
    FW_TXMIT_IDLE = 0, // waiting for a new packet to be ready to send
    FW_TXMIT_HEADER,   // waiting to transmit START_PACKET symbol
    FW_TXMIT_BODY,     // waiting to transmit data characters in packet
    FW_TXMIT_FOOTER,   // waiting to transmit END_PACKET symbol
};

// random interval in the range [3ms, 10ms)
static uint64_t handshake_period(void) {
    uint64_t ms = 1000 * 1000;
    return (rand() % (7 * ms)) + 3 * ms;
}

void fakewire_exc_exchange_loop(void *fwe_opaque) {
    fw_exchange_t *fwe = (fw_exchange_t *) fwe_opaque;
    assert(fwe != NULL);

    enum exchange_state exc_state   = FW_EXC_CONNECTING;
    enum receive_state  recv_state  = FW_RECV_PREPARING;
    enum transmit_state txmit_state = FW_TXMIT_IDLE;

    uint64_t next_timeout = clock_timestamp_monotonic() + handshake_period();

    uint32_t send_handshake_id = 0; // generated handshake ID if in HANDSHAKING mode
    uint32_t recv_handshake_id = 0; // received handshake ID
    bool     send_secondary_handshake = false;

    uint32_t fcts_sent = 0;
    uint32_t fcts_rcvd = 0;
    uint32_t pkts_sent = 0;
    uint32_t pkts_rcvd = 0;
    bool resend_fcts = false;
    bool resend_pkts = false;
    bool send_primary_handshake = false;

    struct io_rx_ent *read_entry = NULL;
    struct io_rx_ent *write_entry = NULL;
    size_t write_offset = 0;

    fw_decoded_ent_t rx_ent = {
        .data_out     = NULL,
        .data_max_len = 0,
    };

    while (true) {
        if (!semaphore_take_try(&fwe->exchange_wake)) {
            // flush encoder before we sleep
            fakewire_enc_flush(&fwe->encoder);

            if (exc_state == FW_EXC_OPERATING && (!resend_fcts || !resend_pkts)) {
                // do a timed wait, so that we can send heartbeats when it's an appropriate time
#ifdef DEBUG
                debug_printf(TRACE, "Blocking in main exchange (timeout A).");
#endif
                if (!semaphore_take_timed_abs(&fwe->exchange_wake, next_timeout)) {
                    assert(clock_timestamp_monotonic() >= next_timeout);
#ifdef DEBUG
                    debug_printf(TRACE, "Woke up main exchange loop (timeout A)");
#endif
                    resend_fcts = true;
                    resend_pkts = true;

                    next_timeout = clock_timestamp_monotonic() + handshake_period();
                }
            } else if ((exc_state == FW_EXC_HANDSHAKING || exc_state == FW_EXC_CONNECTING) && !send_primary_handshake) {
                // do a timed wait, so that we can send a fresh handshake when it's an appropriate time
#ifdef DEBUG
                debug_printf(TRACE, "Blocking in main exchange (timeout B).");
#endif
                if (!semaphore_take_timed_abs(&fwe->exchange_wake, next_timeout)) {
                    assert(clock_timestamp_monotonic() >= next_timeout);
#ifdef DEBUG
                    debug_printf(TRACE, "Woke up main exchange loop (timeout B)");
#endif
                    send_primary_handshake = true;

                    next_timeout = clock_timestamp_monotonic() + handshake_period();
                }
            } else {
#ifdef DEBUG
                debug_printf(TRACE, "Blocking in main exchange (blocking).");
#endif
                semaphore_take(&fwe->exchange_wake);
            }
#ifdef DEBUG
            debug_printf(TRACE, "Woke up main exchange loop");
#endif
        }

        // check invariants
        assert(exc_state >= FW_EXC_CONNECTING && exc_state <= FW_EXC_OPERATING);
        assertf(pkts_sent == fcts_rcvd || pkts_sent + 1 == fcts_rcvd,
                "pkts_sent = %u, fcts_rcvd = %u", pkts_sent, fcts_rcvd);

        // keep receiving line data as long as there's more data to receive; we don't want to sleep until there's
        // nothing left, so that we can guarantee a wakeup will still be pending afterwards,
        for (;;) {
            bool do_reset = false;

            if (exc_state == FW_EXC_OPERATING && recv_state == FW_RECV_RECEIVING
                    && read_entry->actual_length < io_rx_size(fwe->read_chart)) {
                assert(read_entry != NULL);
                rx_ent.data_out     = read_entry->data + read_entry->actual_length;
                rx_ent.data_max_len = io_rx_size(fwe->read_chart) - read_entry->actual_length;
            } else {
                // tell decoder to discard data and just tell us the number of bytes
                rx_ent.data_out     = NULL;
                rx_ent.data_max_len = 0;
            }
            if (!fakewire_dec_decode(&fwe->decoder, &rx_ent)) {
                // no more data to receive right now; wait until next wakeup
                break;
            }
            // process received control character or data characters
            if (rx_ent.ctrl_out != FWC_NONE) {
                assert(rx_ent.data_actual_len == 0);

                fw_ctrl_t symbol = rx_ent.ctrl_out;
                uint32_t  param  = rx_ent.ctrl_param;
#ifdef DEBUG
                debug_printf(TRACE, "Received control character: %s(0x%08x).", fakewire_codec_symbol(symbol), param);
#endif
                assert(param == 0 || fakewire_is_parametrized(symbol));

                switch (exc_state) {
                case FW_EXC_CONNECTING:
                    if (symbol == FWC_HANDSHAKE_1) {
                        // received a primary handshake
                        debug_printf(DEBUG, "Received a primary handshake with ID=0x%08x.", param);
                        recv_handshake_id = param;
                        send_secondary_handshake = true;
                    } else {
                        // There's no point in being loud about this; if we're seeing it, we're ALREADY in a broken
                        // state, and continuing to spew messages about how everything is still broken is not helpful.
                        debug_printf(TRACE, "Unexpected %s(0x%08x) instead of HANDSHAKE_1(*); resetting.",
                                     fakewire_codec_symbol(symbol), param);
                        do_reset = true;
                    }
                    break;
                case FW_EXC_HANDSHAKING:
                    if (symbol == FWC_HANDSHAKE_2 && param == send_handshake_id) {
                        // received a valid secondary handshake
                        debug_printf(DEBUG,
                                     "Received secondary handshake with ID=0x%08x; transitioning to operating mode.",
                                     param);
                        exc_state = FW_EXC_OPERATING;
                        send_primary_handshake = false;
                        send_secondary_handshake = false;
                    } else {
                        debug_printf(CRITICAL, "Unexpected %s(0x%08x) instead of HANDSHAKE_2(0x%08x); resetting.",
                                     fakewire_codec_symbol(symbol), param,
                                     send_handshake_id);
                        do_reset = true;
                    }
                    break;
                case FW_EXC_OPERATING:
                    // TODO: act on any received FWC_HANDSHAKE_1 requests immediately.
                    switch (symbol) {
                    case FWC_START_PACKET:
                        if (fcts_sent != pkts_rcvd + 1) {
                            debug_printf(CRITICAL, "Received unauthorized start-of-packet "
                                         "(fcts_sent=%u, pkts_rcvd=%u); resetting.", fcts_sent, pkts_rcvd);
                            do_reset = true;
                        } else {
                            assert(recv_state == FW_RECV_LISTENING);
                            assert(read_entry != NULL && read_entry->actual_length == 0);
                            recv_state = FW_RECV_RECEIVING;
                            read_entry->receive_timestamp = rx_ent.receive_timestamp;
                            pkts_rcvd += 1;
                            // reset receive buffer before proceeding
                            memset(read_entry->data, 0, io_rx_size(fwe->read_chart));
                        }
                        break;
                    case FWC_END_PACKET:
                        if (recv_state == FW_RECV_OVERFLOWED) {
                            // discard state and get ready for another packet
                            recv_state = FW_RECV_PREPARING;
                            read_entry = NULL;
                        } else if (recv_state == FW_RECV_RECEIVING) {
                            assert(read_entry != NULL);
                            // notify read task that data is ready to consume
                            chart_request_send(fwe->read_chart, 1);
                            recv_state = FW_RECV_PREPARING;
                            read_entry = NULL;
                        } else {
                            debug_printf(CRITICAL, "Hit unexpected END_PACKET in receive state %d; resetting.",
                                         recv_state);
                            do_reset = true;
                        }
                        break;
                    case FWC_ERROR_PACKET:
                        if (recv_state == FW_RECV_OVERFLOWED || recv_state == FW_RECV_RECEIVING) {
                            // discard state and get ready for another packet
                            recv_state = FW_RECV_PREPARING;
                            read_entry = NULL;
                        } else {
                            debug_printf(CRITICAL, "Hit unexpected ERROR_PACKET in receive state %d; resetting.",
                                         recv_state);
                            do_reset = true;
                        }
                        break;
                    case FWC_FLOW_CONTROL:
                        if (param == fcts_rcvd + 1) {
                            // make sure this FCT matches our send state
                            if (pkts_sent != fcts_rcvd) {
                                debug_printf(CRITICAL, "Received incremented FCT(%u) when no packet had been sent "
                                             "(%u, %u); resetting.", param, pkts_sent, fcts_rcvd);
                                do_reset = true;
                            } else {
                                // received FCT; can send another packet
                                fcts_rcvd = param;
                            }
                        } else if (param != fcts_rcvd) {
                            // FCT number should always either stay the same or increment by one.
                            debug_printf(CRITICAL, "Received unexpected FCT(%u) when last count was %u; resetting.",
                                         param, fcts_rcvd);
                            do_reset = true;
                        }
                        break;
                    case FWC_KEEP_ALIVE:
                        if (pkts_rcvd != param) {
                            debug_printf(CRITICAL, "KAT mismatch: received %u packets, but supposed to have received "
                                         "%u; resetting.", pkts_rcvd, param);
                            do_reset = true;
                        }
                        break;
                    default:
                        debug_printf(CRITICAL, "Unexpected %s(0x%08x) during OPERATING mode; resetting.",
                                     fakewire_codec_symbol(symbol), param);
                        do_reset = true;
                    }
                    break;
                default:
                    assert(false);
                }
            } else {
                assert(rx_ent.data_actual_len > 0);

                if (recv_state == FW_RECV_OVERFLOWED) {
                    assert(exc_state == FW_EXC_OPERATING);
                    assert(rx_ent.data_out == NULL);
                    // discard extraneous bytes and do nothing
#ifdef DEBUG
                    debug_printf(DEBUG, "Discarded an additional %zu regular data bytes.", input_len);
#endif
                } else if (exc_state != FW_EXC_OPERATING || recv_state != FW_RECV_RECEIVING) {
                    assert(rx_ent.data_out == NULL);
                    debug_printf(CRITICAL, "Received at least %zu unexpected data characters during "
                                 "state (exc=%d, recv=%d); resetting.", rx_ent.data_actual_len, exc_state, recv_state);
                    do_reset = true;
                } else if (read_entry->actual_length >= io_rx_size(fwe->read_chart)) {
                    assert(rx_ent.data_out == NULL);
                    debug_printf(CRITICAL, "Packet exceeded buffer size %zu (at least %zu + %zu bytes); discarding.",
                                 io_rx_size(fwe->read_chart), read_entry->actual_length, rx_ent.data_actual_len);
                    recv_state = FW_RECV_OVERFLOWED;
                } else {
                    assert(rx_ent.data_out != NULL);
                    assert(read_entry->actual_length + rx_ent.data_actual_len <= io_rx_size(fwe->read_chart));
#ifdef DEBUG
                    debug_printf(TRACE, "Received %zu regular data bytes.", rx_ent.data_actual_len);
#endif
                    read_entry->actual_length += rx_ent.data_actual_len;
                    assert(read_entry->actual_length <= io_rx_size(fwe->read_chart));
                }
            }

            if (do_reset) {
                exc_state = FW_EXC_CONNECTING;
                // unless we're busy, reset receive state
                recv_state = FW_RECV_PREPARING;
                read_entry = NULL;
                // if we're transmitting, make sure we start again from the beginning
                if (txmit_state != FW_TXMIT_IDLE) {
                    txmit_state = FW_TXMIT_HEADER;
                }
                send_handshake_id = 0;
                recv_handshake_id = 0;
                send_primary_handshake = false;
                send_secondary_handshake = false;
                fcts_sent = fcts_rcvd = pkts_sent = pkts_rcvd = 0;
                resend_pkts = resend_fcts = false;
            }
        }

        // we only need to check for starting reads once per iteration, because we are gated on decoded line data
        if (read_entry == NULL) {
            read_entry = chart_request_start(fwe->read_chart);
            if (read_entry != NULL) {
                read_entry->actual_length = 0;
            }
        }

        if (exc_state == FW_EXC_OPERATING && recv_state == FW_RECV_PREPARING && read_entry != NULL) {
            assert(read_entry->actual_length == 0);
#ifdef DEBUG
            debug_printf(TRACE, "Sending FCT.");
#endif
            fcts_sent += 1;
            recv_state = FW_RECV_LISTENING;
            resend_fcts = true;
            resend_pkts = true;

            next_timeout = clock_timestamp_monotonic() + handshake_period();
        }

        if (resend_fcts && fakewire_enc_encode_ctrl(&fwe->encoder, FWC_FLOW_CONTROL, fcts_sent)) {
            assert(exc_state == FW_EXC_OPERATING);
            resend_fcts = false;
#ifdef DEBUG
            debug_printf(TRACE, "Transmitted reminder FCT(%u) tokens.", fcts_sent);
#endif
        }

        if (resend_pkts && fakewire_enc_encode_ctrl(&fwe->encoder, FWC_KEEP_ALIVE, pkts_sent)) {
            assert(exc_state == FW_EXC_OPERATING);
            resend_pkts = false;
#ifdef DEBUG
            debug_printf(TRACE, "Transmitted reminder KAT(%u) tokens.", pkts_sent);
#endif
        }

        if (send_primary_handshake) {
            assert(exc_state == FW_EXC_HANDSHAKING || exc_state == FW_EXC_CONNECTING);

            // pick something very likely to be distinct (Go picks msb unset, C picks msb set)
            uint32_t gen_handshake_id = 0x80000000 + (0x7FFFFFFF & (uint32_t) clock_timestamp_monotonic());

            if (fakewire_enc_encode_ctrl(&fwe->encoder, FWC_HANDSHAKE_1, gen_handshake_id)) {
                send_handshake_id = gen_handshake_id;

                exc_state = FW_EXC_HANDSHAKING;
                send_primary_handshake = false;
                send_secondary_handshake = false;

                debug_printf(DEBUG, "Sent primary handshake with ID=0x%08x; transitioning to handshaking mode.",
                             send_handshake_id);
            }
        }

        if (send_secondary_handshake && fakewire_enc_encode_ctrl(&fwe->encoder, FWC_HANDSHAKE_2, recv_handshake_id)) {
            assert(exc_state == FW_EXC_CONNECTING);

            exc_state = FW_EXC_OPERATING;
            send_primary_handshake = false;
            send_secondary_handshake = false;

            debug_printf(DEBUG, "Sent secondary handshake with ID=0x%08x; transitioning to operating mode.",
                         recv_handshake_id);

            next_timeout = clock_timestamp_monotonic() + handshake_period();
        }

        do {
            if (txmit_state == FW_TXMIT_IDLE) {
                assert(write_entry == NULL);
                write_entry = chart_reply_start(fwe->write_chart);
                if (write_entry != NULL) {
                    assert(write_entry->actual_length > 0);
                    write_offset = 0;
                    txmit_state = FW_TXMIT_HEADER;
                } else {
                    // no more write requests
                    break;
                }
            }

            if (exc_state == FW_EXC_OPERATING && txmit_state == FW_TXMIT_HEADER && pkts_sent + 1 == fcts_rcvd
                    && fakewire_enc_encode_ctrl(&fwe->encoder, FWC_START_PACKET, 0)) {
                assert(write_entry != NULL && write_offset == 0);

                txmit_state = FW_TXMIT_BODY;
                pkts_sent++;
            }

            if (exc_state == FW_EXC_OPERATING && txmit_state == FW_TXMIT_BODY) {
                assert(write_entry != NULL && write_offset < write_entry->actual_length);

                size_t actually_written = fakewire_enc_encode_data(&fwe->encoder, write_entry->data + write_offset,
                                                                   write_entry->actual_length - write_offset);
                if (actually_written + write_offset == write_entry->actual_length) {
                    txmit_state = FW_TXMIT_FOOTER;
                } else {
                    assert(actually_written < write_entry->actual_length - write_offset);
                    write_offset += actually_written;
                }
            }

            if (exc_state == FW_EXC_OPERATING && txmit_state == FW_TXMIT_FOOTER
                    && fakewire_enc_encode_ctrl(&fwe->encoder, FWC_END_PACKET, 0)) {
                assert(write_entry != NULL);

                // respond to writer
                chart_reply_send(fwe->write_chart, 1);

                // reset our state
                txmit_state = FW_TXMIT_IDLE;
                write_entry = NULL;
                write_offset = 0;
            }

            // we want to keep trying to transmit until we either a) run out of pending write requests, or b) run out
            // of encoding buffer space to write those requests. That way, we can be guaranteed that there will be a
            // wakeup pending if there's anything more to do.
        } while (txmit_state == FW_TXMIT_IDLE);
    }
}
