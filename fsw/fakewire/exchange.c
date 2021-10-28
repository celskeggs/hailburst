#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <hal/thread.h>
#include <fsw/clock.h>
#include <fsw/debug.h>
#include <fsw/fakewire/exchange.h>

static void *fakewire_exc_exchange_loop(void *fwe_opaque);

//#define DEBUG
//#define APIDEBUG

#define debug_puts(str) (debugf("[  fakewire_exc] [%s] %s", fwe->link_opts.label, str))
#define debug_printf(fmt, ...) (debugf("[  fakewire_exc] [%s] " fmt, fwe->link_opts.label, __VA_ARGS__))

struct input_queue_ent {
    enum {
        INPUT_WRITE_PACKET,
        INPUT_WAKEUP,  // used by the transmit thread to make sure the transmit chart is rechecked
    } type;
    union {
        struct {
            uint8_t *packet_in;
            size_t   packet_len;
            wakeup_t on_complete;
        } write_packet;
        /* no parameters on wakeup */
    };
};

static void fakewire_exc_chart_notify_exchange(void *opaque) {
    fw_exchange_t *fwe = (fw_exchange_t *) opaque;
    assert(fwe != NULL);

    // we only need to send if the queue is empty... this is because ANY message qualifies as a wakeup in addition to
    // its primary meaning! so any wakeup we add would be redundant.
    if (queue_is_empty(&fwe->input_queue)) {
        struct input_queue_ent entry = {
            .type = INPUT_WAKEUP,
        };
        // if this send doesn't succeed, no worries! that means the queue somehow got filled since we checked whether
        // it was empty, and in that case, there's a wakeup now!
        (void) queue_send_try(&fwe->input_queue, &entry);
    }
}

int fakewire_exc_init(fw_exchange_t *fwe, fw_link_options_t link_opts, chart_t *read_chart) {
    assert(fwe != NULL && read_chart != NULL);
    memset(fwe, 0, sizeof(fw_exchange_t));

    fwe->link_opts = link_opts;

    fwe->read_chart = read_chart;
    chart_attach_client(fwe->read_chart, fakewire_exc_chart_notify_exchange, fwe);

    queue_init(&fwe->input_queue, sizeof(struct input_queue_ent), 16);
    chart_init(&fwe->transmit_chart, 1024, 16);
    chart_attach_client(&fwe->transmit_chart, fakewire_exc_chart_notify_exchange, fwe);
    chart_init(&fwe->receive_chart, 1024, 16);
    chart_attach_server(&fwe->receive_chart, fakewire_exc_chart_notify_exchange, fwe);
    semaphore_init(&fwe->write_ready_sem);

    fakewire_enc_init(&fwe->encoder, &fwe->transmit_chart);
    fakewire_dec_init(&fwe->decoder, &fwe->receive_chart);

    if (fakewire_link_init(&fwe->io_port, link_opts, &fwe->receive_chart, &fwe->transmit_chart) < 0) {
        semaphore_destroy(&fwe->write_ready_sem);
        chart_destroy(&fwe->receive_chart);
        chart_destroy(&fwe->transmit_chart);
        queue_destroy(&fwe->input_queue);
        return -1;
    }

    thread_create(&fwe->exchange_thread, "fw_exc_thread", PRIORITY_SERVERS, fakewire_exc_exchange_loop, fwe, NOT_RESTARTABLE);

    return 0;
}

void fakewire_exc_write(fw_exchange_t *fwe, uint8_t *packet_in, size_t packet_len) {
    assert(fwe != NULL);

#ifdef APIDEBUG
    debug_printf("API write(%zu bytes) start", packet_len);
#endif

    // wait until a write can be submitted
    semaphore_take(&fwe->write_ready_sem);

    // submit the write
    struct input_queue_ent entry = {
        .type = INPUT_WRITE_PACKET,
        .write_packet = {
            .packet_in   = packet_in,
            .packet_len  = packet_len,
            .on_complete = wakeup_open(),
        },
    };
    queue_send(&fwe->input_queue, &entry);

    // wait until write completes, so that we know when we can reuse the packet_in pointer
    wakeup_take(entry.write_packet.on_complete);

#ifdef APIDEBUG
    debug_printf("API write(%zu bytes) success", packet_len);
#endif
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

static void *fakewire_exc_exchange_loop(void *fwe_opaque) {
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

    uint8_t *cur_packet_in     = NULL;
    size_t   cur_packet_len    = 0;
    wakeup_t cur_packet_wakeup = NULL;

    fw_decoded_ent_t rx_ent = {
        .data_out     = NULL,
        .data_max_len = 0,
    };

    struct input_queue_ent input_ent;

    // make sure we accept input from the first writer
    semaphore_give(&fwe->write_ready_sem);

    while (true) {
        bool timed_out = false;
        // start by checking whether there's a queue entry already available
        if (!queue_recv_try(&fwe->input_queue, &input_ent)) {
            // flush encoder before we sleep
            fakewire_enc_flush(&fwe->encoder);

            // event loop centered around the input queue... this should be the ONLY blocking call in this thread!
            if (exc_state == FW_EXC_OPERATING ? (!resend_fcts || !resend_pkts) : !send_primary_handshake) {
                // explanation of this conditional: once we've timed out already and set the appropriate flags,
                // there is no reason to keep timing out just to set the very same flags again.
#ifdef DEBUG
                debug_puts("Blocking in main exchange (with timeout).");
#endif
                timed_out = !queue_recv_timed_abs(&fwe->input_queue, &input_ent, next_timeout);
            } else {
#ifdef DEBUG
                debug_puts("Blocking in main exchange (blocking).");
#endif
                queue_recv(&fwe->input_queue, &input_ent);
            }
        }
#ifdef DEBUG
        const char *wakeup_explanation = "???";
        if (timed_out) {
            wakeup_explanation = "timed out";
        } else {
            switch (input_ent.type) {
            case INPUT_WRITE_PACKET:
                wakeup_explanation = "INPUT_WRITE_PACKET";
                break;
            case INPUT_WAKEUP:
                wakeup_explanation = "INPUT_WAKEUP";
                break;
            }
        }
        debug_printf("Woke up main exchange loop (%s)", wakeup_explanation);
#endif

        // check invariants
        assert(exc_state >= FW_EXC_CONNECTING && exc_state <= FW_EXC_OPERATING);
        assertf(pkts_sent == fcts_rcvd || pkts_sent + 1 == fcts_rcvd,
                "pkts_sent = %u, fcts_rcvd = %u", pkts_sent, fcts_rcvd);

        if (timed_out) {
            assert(clock_timestamp_monotonic() >= next_timeout);

            if (exc_state == FW_EXC_OPERATING) {
                resend_fcts = true;
                resend_pkts = true;
            } else {
                assert(exc_state == FW_EXC_HANDSHAKING || exc_state == FW_EXC_CONNECTING);
                send_primary_handshake = true;
            }

            next_timeout = clock_timestamp_monotonic() + handshake_period();
        } else if (input_ent.type == INPUT_WRITE_PACKET) {
            assert(txmit_state == FW_TXMIT_IDLE && cur_packet_in == NULL && cur_packet_wakeup == NULL);
            cur_packet_in     = input_ent.write_packet.packet_in;
            cur_packet_len    = input_ent.write_packet.packet_len;
            cur_packet_wakeup = input_ent.write_packet.on_complete;
            txmit_state = FW_TXMIT_HEADER;
            assert(cur_packet_in != NULL && cur_packet_wakeup != NULL);
        } else if (input_ent.type == INPUT_WAKEUP) {
            // no need to do anything... the whole point is just to wake us up immediately.
        } else {
            assert(false);
        }

        // input byte decode loop
        for (;;) {
            bool do_reset = false;

            if (exc_state == FW_EXC_OPERATING && recv_state == FW_RECV_RECEIVING && read_entry->actual_length < io_rx_size(fwe->read_chart)) {
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
                debug_printf("Received control character: %s(0x%08x).", fakewire_codec_symbol(symbol), param);
#endif
                assert(param == 0 || fakewire_is_parametrized(symbol));

                switch (exc_state) {
                case FW_EXC_CONNECTING:
                    if (symbol == FWC_HANDSHAKE_1) {
                        // received a primary handshake
                        debug_printf("Received a primary handshake with ID=0x%08x.", param);
                        recv_handshake_id = param;
                        send_secondary_handshake = true;
                    } else {
                        debug_printf("Unexpected %s(0x%08x) instead of HANDSHAKE_1(*); resetting.",
                                     fakewire_codec_symbol(symbol), param);
                        do_reset = true;
                    }
                    break;
                case FW_EXC_HANDSHAKING:
                    if (symbol == FWC_HANDSHAKE_2 && param == send_handshake_id) {
                        // received a valid secondary handshake
                        debug_printf("Received secondary handshake with ID=0x%08x; transitioning to operating mode.",
                                     param);
                        exc_state = FW_EXC_OPERATING;
                        send_primary_handshake = false;
                        send_secondary_handshake = false;
                    } else {
                        debug_printf("Unexpected %s(0x%08x) instead of HANDSHAKE_2(0x%08x); resetting.",
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
                            debug_printf("Received unauthorized start-of-packet (fcts_sent=%u, pkts_rcvd=%u); resetting.",
                                         fcts_sent, pkts_rcvd);
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
                            chart_request_send(fwe->read_chart, read_entry);
                            recv_state = FW_RECV_PREPARING;
                            read_entry = NULL;
                        } else {
                            debug_printf("Hit unexpected END_PACKET in receive state %d; resetting.", recv_state);
                            do_reset = true;
                        }
                        break;
                    case FWC_ERROR_PACKET:
                        if (recv_state == FW_RECV_OVERFLOWED || recv_state == FW_RECV_RECEIVING) {
                            // discard state and get ready for another packet
                            recv_state = FW_RECV_PREPARING;
                            read_entry = NULL;
                        } else {
                            debug_printf("Hit unexpected ERROR_PACKET in receive state %d; resetting.", recv_state);
                            do_reset = true;
                        }
                        break;
                    case FWC_FLOW_CONTROL:
                        if (param == fcts_rcvd + 1) {
                            // make sure this FCT matches our send state
                            if (pkts_sent != fcts_rcvd) {
                                debug_printf("Received incremented FCT(%u) when no packet had been sent (%u, %u); resetting.",
                                             param, pkts_sent, fcts_rcvd);
                                do_reset = true;
                            } else {
                                // received FCT; can send another packet
                                fcts_rcvd = param;
                            }
                        } else if (param != fcts_rcvd) {
                            // FCT number should always either stay the same or increment by one.
                            debug_printf("Received unexpected FCT(%u) when last count was %u; resetting.",
                                         param, fcts_rcvd);
                            do_reset = true;
                        }
                        break;
                    case FWC_KEEP_ALIVE:
                        if (pkts_rcvd != param) {
                            debug_printf("KAT mismatch: received %u packets, but supposed to have received %u; resetting.",
                                         pkts_rcvd, param);
                            do_reset = true;
                        }
                        break;
                    default:
                        debug_printf("Unexpected %s(0x%08x) during OPERATING mode; resetting.",
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
                    debug_printf("Discarded an additional %zu regular data bytes.", input_len);
#endif
                } else if (exc_state != FW_EXC_OPERATING || recv_state != FW_RECV_RECEIVING) {
                    assert(rx_ent.data_out == NULL);
                    debug_printf("Received at least %zu unexpected data characters during state (exc=%d, recv=%d); "
                                 "resetting.", rx_ent.data_actual_len, exc_state, recv_state);
                    do_reset = true;
                } else if (read_entry->actual_length >= io_rx_size(fwe->read_chart)) {
                    assert(rx_ent.data_out == NULL);
                    debug_printf("Packet exceeded buffer size %zu (at least %zu + %z bytes); discarding.",
                                 io_rx_size(fwe->read_chart), read_entry->actual_length, rx_ent.data_actual_len);
                    recv_state = FW_RECV_OVERFLOWED;
                } else {
                    assert(rx_ent.data_out != NULL);
                    assert(read_entry->actual_length + rx_ent.data_actual_len <= io_rx_size(fwe->read_chart));
#ifdef DEBUG
                    debug_printf("Received %zu regular data bytes.", rx_ent.data_actual_len);
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

        if (read_entry == NULL) {
            struct io_rx_ent *ent;
            while ((ent = chart_ack_start(fwe->read_chart)) != NULL) {
                chart_ack_send(fwe->read_chart, ent);
            }

            read_entry = chart_request_start(fwe->read_chart);
            if (read_entry != NULL) {
                read_entry->actual_length = 0;
            }
        }

        if (exc_state == FW_EXC_OPERATING && recv_state == FW_RECV_PREPARING && read_entry != NULL) {
            assert(read_entry->actual_length == 0);
#ifdef DEBUG
            debug_puts("Sending FCT.");
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
            debug_printf("Transmitted reminder FCT(%u) tokens.", fcts_sent);
#endif
        }

        if (resend_pkts && fakewire_enc_encode_ctrl(&fwe->encoder, FWC_KEEP_ALIVE, pkts_sent)) {
            assert(exc_state == FW_EXC_OPERATING);
            resend_pkts = false;
#ifdef DEBUG
            debug_printf("Transmitted reminder KAT(%u) tokens.", pkts_sent);
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

                debug_printf("Sent primary handshake with ID=0x%08x; transitioning to handshaking mode.",
                             send_handshake_id);
            }
        }

        if (send_secondary_handshake && fakewire_enc_encode_ctrl(&fwe->encoder, FWC_HANDSHAKE_2, recv_handshake_id)) {
            assert(exc_state == FW_EXC_CONNECTING);

            exc_state = FW_EXC_OPERATING;
            send_primary_handshake = false;
            send_secondary_handshake = false;

            debug_printf("Sent secondary handshake with ID=0x%08x; transitioning to operating mode.",
                         recv_handshake_id);

            next_timeout = clock_timestamp_monotonic() + handshake_period();
        }

        if (exc_state == FW_EXC_OPERATING && txmit_state == FW_TXMIT_HEADER && pkts_sent + 1 == fcts_rcvd
                && fakewire_enc_encode_ctrl(&fwe->encoder, FWC_START_PACKET, 0)) {
            assert(cur_packet_in != NULL);

            txmit_state = FW_TXMIT_BODY;
            pkts_sent++;
        }

        if (exc_state == FW_EXC_OPERATING && txmit_state == FW_TXMIT_BODY) {
            assert(cur_packet_in != NULL);

            size_t actually_written = fakewire_enc_encode_data(&fwe->encoder, cur_packet_in, cur_packet_len);
            if (actually_written == cur_packet_len) {
                txmit_state = FW_TXMIT_FOOTER;
            } else {
                assert(actually_written < cur_packet_len);
                cur_packet_in  += actually_written;
                cur_packet_len -= actually_written;
            }
        }

        if (exc_state == FW_EXC_OPERATING && txmit_state == FW_TXMIT_FOOTER
                && fakewire_enc_encode_ctrl(&fwe->encoder, FWC_END_PACKET, 0)) {
            assert(cur_packet_in != NULL);

            // wake up writer
            wakeup_give(cur_packet_wakeup);

            // reset our state
            txmit_state = FW_TXMIT_IDLE;
            cur_packet_in     = NULL;
            cur_packet_len    = 0;
            cur_packet_wakeup = NULL;

            // tell the next writer we're ready to hear from it
            bool given = semaphore_give(&fwe->write_ready_sem);
            assert(given == true);
        }
    }
}
