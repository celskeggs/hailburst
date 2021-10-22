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
static void *fakewire_exc_read_cb_loop(void *fwe_opaque);
static void *fakewire_exc_transmit_loop(void *fwe_opaque);

static void fakewire_exc_on_recv_data(void *opaque, uint8_t *bytes_in, size_t bytes_count);
static void fakewire_exc_on_recv_ctrl(void *opaque, fw_ctrl_t symbol, uint32_t param, uint64_t timestamp_ns);

//#define DEBUG
//#define APIDEBUG

#define debug_puts(str) (debugf("[  fakewire_exc] [%s] %s", fwe->options.link_options.label, str))
#define debug_printf(fmt, ...) (debugf("[  fakewire_exc] [%s] " fmt, fwe->options.link_options.label, __VA_ARGS__))

struct input_queue_ent {
    enum {
        INPUT_RECV_CTRL_CHAR,
        INPUT_RECV_DATA_CHARS,
        INPUT_READ_CB_COMPLETE,
        INPUT_WRITE_PACKET,
        INPUT_WAKEUP,  // used by the transmit thread to make sure the transmit chart is rechecked
    } type;
    union {
        struct {
            fw_ctrl_t symbol;
            uint32_t  param;
            uint64_t  timestamp_ns;
        } ctrl_char;
        struct {
            uint8_t *input_ptr;
            size_t   input_len;
            wakeup_t on_complete;
        } data_chars;
        /* no parameters on read_cb_complete */
        struct {
            uint8_t *packet_in;
            size_t   packet_len;
            wakeup_t on_complete;
        } write_packet;
        /* no parameters on wakeup */
    };
};

struct transmit_chart_ent {
    // <request region>
    fw_ctrl_t symbol; // if FWC_NONE, indicates a data character entry
    union {
        uint32_t ctrl_param;
        struct {
            uint8_t *data_ptr;
            size_t   data_len;
        } data_param;
    };
    // <reply region>
    // (empty)
};

struct read_cb_queue_ent {
    /* buffer pointer not necessary, because it's always 'recv_buffer' in fw_exchange_t */
    size_t   read_size;
    uint64_t timestamp_ns; // timestamp when START_PACKET character was received
};

static void fakewire_exc_link_write(void *opaque, uint8_t *bytes_in, size_t bytes_count) {
    fw_exchange_t *fwe = (fw_exchange_t *) opaque;
    assert(fwe != NULL && bytes_in != NULL);

    fakewire_link_write(&fwe->io_port, bytes_in, bytes_count);
}

static void fakewire_exc_transmit_chart_notify_server(void *opaque) {
    fw_exchange_t *fwe = (fw_exchange_t *) opaque;
    assert(fwe != NULL);

    // we ignore the return value... if this fails, that's not a problem! that just means there was already a wake
    // pending for the transmit thread, which is perfectly fine.
    (void) semaphore_give(&fwe->transmit_wake);
}

static void fakewire_exc_transmit_chart_notify_client(void *opaque) {
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

int fakewire_exc_init(fw_exchange_t *fwe, fw_exchange_options_t opts) {
    memset(fwe, 0, sizeof(fw_exchange_t));

    queue_init(&fwe->input_queue, sizeof(struct input_queue_ent), 16);
    chart_init(&fwe->transmit_chart, sizeof(struct transmit_chart_ent), 16,
               fakewire_exc_transmit_chart_notify_server, fakewire_exc_transmit_chart_notify_client, fwe);
    semaphore_init(&fwe->transmit_wake);
    queue_init(&fwe->read_cb_queue, sizeof(struct read_cb_queue_ent), 1);
    semaphore_init(&fwe->write_ready_sem);

    fwe->options = opts;
    fwe->link_interface = (fw_receiver_t) {
        .param = fwe,
        .recv_data = fakewire_exc_on_recv_data,
        .recv_ctrl = fakewire_exc_on_recv_ctrl,
    };

    assert(opts.recv_max_size >= 1);
    fwe->recv_buffer = malloc(opts.recv_max_size);
    assert(fwe->recv_buffer != NULL);

    fakewire_enc_init(&fwe->encoder, fakewire_exc_link_write, fwe);

    if (fakewire_link_init(&fwe->io_port, &fwe->link_interface, opts.link_options) < 0) {
        free(fwe->recv_buffer);
        semaphore_destroy(&fwe->write_ready_sem);
        queue_destroy(&fwe->read_cb_queue);
        semaphore_destroy(&fwe->transmit_wake);
        chart_destroy(&fwe->transmit_chart);
        queue_destroy(&fwe->input_queue);
        return -1;
    }

    thread_create(&fwe->exchange_thread, "fw_exc_thread",      PRIORITY_SERVERS, fakewire_exc_exchange_loop, fwe, NOT_RESTARTABLE);
    thread_create(&fwe->read_cb_thread,  "fw_read_cb_thread",  PRIORITY_SERVERS, fakewire_exc_read_cb_loop,  fwe, NOT_RESTARTABLE);
    thread_create(&fwe->transmit_thread, "fw_transmit_thread", PRIORITY_SERVERS, fakewire_exc_transmit_loop, fwe, RESTARTABLE);
    return 0;
}

static void fakewire_exc_on_recv_data(void *opaque, uint8_t *bytes_in, size_t bytes_count) {
    fw_exchange_t *fwe = (fw_exchange_t *) opaque;
    assert(fwe != NULL && bytes_in != NULL);

    struct input_queue_ent entry = {
        .type = INPUT_RECV_DATA_CHARS,
        .data_chars = {
            .input_ptr   = bytes_in,
            .input_len   = bytes_count,
            .on_complete = wakeup_open(),
        },
    };
    queue_send(&fwe->input_queue, &entry);

    // must wait so that we know when the *bytes_in buffer can be reused.
    wakeup_take(entry.data_chars.on_complete);
}

static void fakewire_exc_on_recv_ctrl(void *opaque, fw_ctrl_t symbol, uint32_t param, uint64_t timestamp_ns) {
    fw_exchange_t *fwe = (fw_exchange_t *) opaque;
    assert(fwe != NULL && fakewire_is_special(symbol));
    assert(param == 0 || fakewire_is_parametrized(symbol));

    struct input_queue_ent entry = {
        .type = INPUT_RECV_CTRL_CHAR,
        .ctrl_char = {
            .symbol       = symbol,
            .param        = param,
            .timestamp_ns = timestamp_ns,
        },
    };
    queue_send(&fwe->input_queue, &entry);

    // no need to wait for this entry to be processed... there's no pointer to free, so we can continue immediately.
}

static void *fakewire_exc_read_cb_loop(void *fwe_opaque) {
    fw_exchange_t *fwe = (fw_exchange_t *) fwe_opaque;
    assert(fwe != NULL);
    assert(fwe->recv_buffer != NULL);

    struct read_cb_queue_ent read_cb_entry;

    while (true) {
        // wait for a callback to dispatch
        queue_recv(&fwe->read_cb_queue, &read_cb_entry);

        // dispatch callback
#ifdef APIDEBUG
        debug_printf("API callback for read(%zd bytes/%zu bytes) starting...", read_cb_entry.read_size, fwe->options.recv_max_size);
#endif
        fwe->options.recv_callback(fwe->options.recv_param, fwe->recv_buffer, read_cb_entry.read_size, read_cb_entry.timestamp_ns);
#ifdef APIDEBUG
        debug_puts("API callback for read completed.");
#endif

        // notify that we are ready for another read
        struct input_queue_ent entry = {
            .type = INPUT_READ_CB_COMPLETE,
        };
        queue_send(&fwe->input_queue, &entry);
    }
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

static void *fakewire_exc_transmit_loop(void *fwe_opaque) {
    fw_exchange_t *fwe = (fw_exchange_t *) fwe_opaque;
    assert(fwe != NULL);
    assert(fwe->recv_buffer != NULL);

    debug_puts("Initializing exchange transmit loop!");

    bool needs_flush = false;

    // when we initialize, if we have a pending send, we MUST skip it.
    // this is because it might have already been processed, and we do not want to write duplicate data to the line!
    void *note = chart_reply_start(&fwe->transmit_chart);
    if (note != NULL) {
        chart_reply_send(&fwe->transmit_chart, note);
        debug_puts("Cleared existing message.");
    }

    while (true) {
        struct transmit_chart_ent *txmit_entry = (struct transmit_chart_ent *) chart_reply_start(&fwe->transmit_chart);
        if (txmit_entry == NULL) {
            // we only need to flush if we're going to block... otherwise, we're fine just squishing adjacent transmits
            // into a single bulk write to the serial port.
            if (needs_flush) {
                fakewire_enc_flush(&fwe->encoder);
            }

            // wait until something is ready, and then check again.
            semaphore_take(&fwe->transmit_wake);
            continue;
        }

        // encode specified data
        if (txmit_entry->symbol == FWC_NONE) {
            assert(txmit_entry->data_param.data_ptr != NULL);
#ifdef DEBUG
            debug_printf("Transmitting %zu data characters.", txmit_entry->data_param.data_len);
#endif
            // data characters
            fakewire_enc_encode_data(&fwe->encoder, txmit_entry->data_param.data_ptr, txmit_entry->data_param.data_len);

            // we don't set needs_flush here, because any important sequence of data characters will normally be
            // followed by a FWC_END_PACKET, which will trigger the actual flush that matters.
        } else {
#ifdef DEBUG
            debug_printf("Transmitting control character %s(0x%08x).", fakewire_codec_symbol(txmit_entry->symbol), txmit_entry->ctrl_param);
#endif
            // control character
            fakewire_enc_encode_ctrl(&fwe->encoder, txmit_entry->symbol, txmit_entry->ctrl_param);

            // we don't set needs_flush on FWC_START_PACKET, because it will normally be followed by a FWC_END_PACKET,
            // which will trigger the actual flush that matters.
            if (txmit_entry->symbol != FWC_START_PACKET) {
                needs_flush = true;
            }
        }

        chart_reply_send(&fwe->transmit_chart, txmit_entry);
    }
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
    FW_RECV_CALLBACK,      // full packet has been received; waiting for callback to complete
};

enum transmit_state {
    FW_TXMIT_IDLE = 0, // waiting for a new packet to be ready to send
    FW_TXMIT_HEADER,   // waiting to transmit START_PACKET symbol
    FW_TXMIT_BODY,     // waiting to transmit data characters in packet
    FW_TXMIT_FOOTER,   // waiting to transmit END_PACKET symbol
    FW_TXMIT_PEND,     // waiting to receive confirmation that data buffer is consumed
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

    uint64_t recv_start_timestamp = 0;
    size_t recv_offset = 0;

    uint8_t *cur_packet_in     = NULL;
    size_t   cur_packet_len    = 0;
    wakeup_t cur_packet_wakeup = NULL;

    struct transmit_chart_ent *tx_ent = NULL;

    struct input_queue_ent input_ent;

    // make sure we accept input from the first writer
    semaphore_give(&fwe->write_ready_sem);

    while (true) {
        // event loop centered around the input queue... this should be the ONLY blocking call in this thread!
        bool timed_out = false;
        if (exc_state == FW_EXC_OPERATING ? (!resend_fcts || !resend_pkts) : !send_primary_handshake) {
#ifdef DEBUG
            debug_puts("Entering main exchange loop (with timeout).");
#endif
            // explanation of this conditional: once we've timed out already and set the appropriate flags,
            // there is no reason to keep timing out just to set the very same flags again.
            timed_out = !queue_recv_timed_abs(&fwe->input_queue, &input_ent, next_timeout);
        } else {
#ifdef DEBUG
            debug_puts("Entering main exchange loop (blocking).");
#endif
            queue_recv(&fwe->input_queue, &input_ent);
        }
#ifdef DEBUG
        const char *wakeup_explanation = "???";
        if (timed_out) {
            wakeup_explanation = "timed out";
        } else {
            switch (input_ent.type) {
            case INPUT_RECV_CTRL_CHAR:
                wakeup_explanation = "INPUT_RECV_CTRL_CHAR";
                break;
            case INPUT_RECV_DATA_CHARS:
                wakeup_explanation = "INPUT_RECV_DATA_CHARS";
                break;
            case INPUT_READ_CB_COMPLETE:
                wakeup_explanation = "INPUT_READ_CB_COMPLETE";
                break;
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

        bool do_reset = false;

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
        } else if (input_ent.type == INPUT_RECV_CTRL_CHAR) {
            fw_ctrl_t symbol = input_ent.ctrl_char.symbol;
            uint32_t  param  = input_ent.ctrl_char.param;
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
                        recv_state = FW_RECV_RECEIVING;
                        recv_start_timestamp = input_ent.ctrl_char.timestamp_ns;
                        pkts_rcvd += 1;
                        // reset receive buffer before proceeding
                        memset(fwe->recv_buffer, 0, fwe->options.recv_max_size);
                        recv_offset = 0;
                    }
                    break;
                case FWC_END_PACKET:
                    if (recv_state == FW_RECV_OVERFLOWED) {
                        // discard state and get ready for another packet
                        recv_state = FW_RECV_PREPARING;
                    } else if (recv_state == FW_RECV_RECEIVING) {
                        // confirm completion
                        recv_state = FW_RECV_CALLBACK;
                        struct read_cb_queue_ent entry = {
                            .read_size    = recv_offset,
                            .timestamp_ns = recv_start_timestamp,
                        };
                        bool sent = queue_send_try(&fwe->read_cb_queue, &entry);
                        assert(sent == true);
                    } else {
                        debug_printf("Hit unexpected END_PACKET in receive state %d; resetting.", recv_state);
                        do_reset = true;
                    }
                    break;
                case FWC_ERROR_PACKET:
                    if (recv_state == FW_RECV_OVERFLOWED || recv_state == FW_RECV_RECEIVING) {
                        // discard state and get ready for another packet
                        recv_state = FW_RECV_PREPARING;
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
        } else if (input_ent.type == INPUT_RECV_DATA_CHARS) {
            uint8_t *input_ptr   = input_ent.data_chars.input_ptr;
            size_t   input_len   = input_ent.data_chars.input_len;
            wakeup_t on_complete = input_ent.data_chars.on_complete;
            assert(input_ptr != NULL && input_len > 0 && on_complete != NULL);

#ifdef DEBUG
            debug_printf("Received %zu regular data bytes.", input_len);
#endif

            if (recv_state == FW_RECV_OVERFLOWED) {
                assert(exc_state == FW_EXC_OPERATING);
                // discard extraneous bytes and do nothing
            } else if (exc_state != FW_EXC_OPERATING || recv_state != FW_RECV_RECEIVING) {
                debug_printf("Received unexpected data character 0x%02x during state (exc=%d, recv=%d); resetting.",
                             input_ptr[0], exc_state, recv_state);
                do_reset = true;
            } else if (recv_offset + input_len > fwe->options.recv_max_size) {
                debug_printf("Packet exceeded buffer size %zu; discarding.", fwe->options.recv_max_size);
                recv_state = FW_RECV_OVERFLOWED;
            } else {
                // actually collect the received data and put it into the buffer
                assert(fwe->recv_buffer != NULL);
                assert(recv_offset < fwe->options.recv_max_size);

                memcpy(&fwe->recv_buffer[recv_offset], input_ptr, input_len);
                recv_offset += input_len;

                assert(recv_offset <= fwe->options.recv_max_size);
            }

            wakeup_give(on_complete);
        } else if (input_ent.type == INPUT_READ_CB_COMPLETE) {
            assert(recv_state == FW_RECV_CALLBACK);
            recv_state = FW_RECV_PREPARING;
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

        if (do_reset) {
            exc_state = FW_EXC_CONNECTING;
            // unless we're busy, reset receive state
            if (recv_state != FW_RECV_CALLBACK) {
                recv_state = FW_RECV_PREPARING;
            }
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

        if (exc_state == FW_EXC_OPERATING && recv_state == FW_RECV_PREPARING) {
#ifdef DEBUG
            debug_puts("Sending FCT.");
#endif
            fcts_sent += 1;
            recv_state = FW_RECV_LISTENING;
            resend_fcts = true;
            resend_pkts = true;

            next_timeout = clock_timestamp_monotonic() + handshake_period();
        }

        // acknowledge any outstanding chart entries
        struct transmit_chart_ent *ack_tx_ent;
        while ((ack_tx_ent = chart_ack_start(&fwe->transmit_chart)) != NULL) {
            if (ack_tx_ent->symbol == FWC_NONE) {
                // if we wrote the data bytes for a packet, then we no longer need to hold on to the active buffer!
                assert(txmit_state == FW_TXMIT_PEND);
                assert(ack_tx_ent->data_param.data_ptr == cur_packet_in);
                assert(ack_tx_ent->data_param.data_len == cur_packet_len);

                // wake up writer
                wakeup_give(cur_packet_wakeup);

                // reset our state
                txmit_state = FW_TXMIT_IDLE;
                cur_packet_in = NULL;
                cur_packet_len = 0;
                cur_packet_wakeup = NULL;

                // tell the next writer we're ready to hear from it
                bool given = semaphore_give(&fwe->write_ready_sem);
                assert(given == true);
            }
            chart_ack_send(&fwe->transmit_chart, ack_tx_ent);
        }

        // check to see if we can transmit now, if we couldn't before
        if (tx_ent == NULL) {
            tx_ent = (struct transmit_chart_ent *) chart_request_start(&fwe->transmit_chart);
        }

        if (tx_ent != NULL && resend_fcts) {
            assert(exc_state == FW_EXC_OPERATING);

            *tx_ent = (struct transmit_chart_ent) {
                .symbol     = FWC_FLOW_CONTROL,
                .ctrl_param = fcts_sent,
            };

            resend_fcts = false;

#ifdef DEBUG
            debug_printf("Transmitting reminder FCT(%u) tokens.", fcts_sent);
#endif
            // send this note and locate the next one, if available
            chart_request_send(&fwe->transmit_chart, tx_ent);
            tx_ent = (struct transmit_chart_ent *) chart_request_start(&fwe->transmit_chart);
        }

        if (tx_ent != NULL && resend_pkts) {
            assert(exc_state == FW_EXC_OPERATING);

            *tx_ent = (struct transmit_chart_ent) {
                .symbol       = FWC_KEEP_ALIVE,
                .ctrl_param   = pkts_sent,
            };

            resend_pkts = false;

#ifdef DEBUG
            debug_printf("Transmitting reminder KAT(%u) tokens.", pkts_sent);
#endif

            // send this note and locate the next one, if available
            chart_request_send(&fwe->transmit_chart, tx_ent);
            tx_ent = (struct transmit_chart_ent *) chart_request_start(&fwe->transmit_chart);
        }

        if (tx_ent != NULL && send_primary_handshake) {
            assert(exc_state == FW_EXC_HANDSHAKING || exc_state == FW_EXC_CONNECTING);

            // pick something very likely to be distinct (Go picks msb unset, C picks msb set)
            send_handshake_id = 0x80000000 + (0x7FFFFFFF & (uint32_t) clock_timestamp_monotonic());

            *tx_ent = (struct transmit_chart_ent) {
                .symbol       = FWC_HANDSHAKE_1,
                .ctrl_param   = send_handshake_id,
            };

            exc_state = FW_EXC_HANDSHAKING;
            send_primary_handshake = false;
            send_secondary_handshake = false;

            debug_printf("Sending primary handshake with ID=0x%08x; transitioning to handshaking mode.",
                         send_handshake_id);

            // send this note and locate the next one, if available
            chart_request_send(&fwe->transmit_chart, tx_ent);
            tx_ent = (struct transmit_chart_ent *) chart_request_start(&fwe->transmit_chart);
        }

        if (tx_ent != NULL && send_secondary_handshake) {
            assert(exc_state == FW_EXC_CONNECTING);

            *tx_ent = (struct transmit_chart_ent) {
                .symbol       = FWC_HANDSHAKE_2,
                .ctrl_param   = recv_handshake_id,
            };

            exc_state = FW_EXC_OPERATING;
            send_primary_handshake = false;
            send_secondary_handshake = false;

            debug_printf("Sending secondary handshake with ID=0x%08x; transitioning to operating mode.",
                         recv_handshake_id);

            next_timeout = clock_timestamp_monotonic() + handshake_period();

            // send this note and locate the next one, if available
            chart_request_send(&fwe->transmit_chart, tx_ent);
            tx_ent = (struct transmit_chart_ent *) chart_request_start(&fwe->transmit_chart);
        }

        if (tx_ent != NULL && exc_state == FW_EXC_OPERATING && txmit_state == FW_TXMIT_HEADER && pkts_sent + 1 == fcts_rcvd) {
            assert(cur_packet_in != NULL);

            *tx_ent = (struct transmit_chart_ent) {
                .symbol       = FWC_START_PACKET,
                .ctrl_param   = 0,
            };

            txmit_state = FW_TXMIT_BODY;
            pkts_sent++;

            // send this note and locate the next one, if available
            chart_request_send(&fwe->transmit_chart, tx_ent);
            tx_ent = (struct transmit_chart_ent *) chart_request_start(&fwe->transmit_chart);
        }

        if (tx_ent != NULL && exc_state == FW_EXC_OPERATING && txmit_state == FW_TXMIT_BODY) {
            assert(cur_packet_in != NULL);

            *tx_ent = (struct transmit_chart_ent) {
                .symbol       = FWC_NONE,
                .data_param   = {
                    .data_ptr = cur_packet_in,
                    .data_len = cur_packet_len,
                },
            };

            txmit_state = FW_TXMIT_FOOTER;

            // send this note and locate the next one, if available
            chart_request_send(&fwe->transmit_chart, tx_ent);
            tx_ent = (struct transmit_chart_ent *) chart_request_start(&fwe->transmit_chart);
        }

        if (tx_ent != NULL && exc_state == FW_EXC_OPERATING && txmit_state == FW_TXMIT_FOOTER) {
            assert(cur_packet_in != NULL);

            *tx_ent = (struct transmit_chart_ent) {
                .symbol       = FWC_END_PACKET,
                .ctrl_param   = 0,
            };

            txmit_state = FW_TXMIT_PEND;

            // send this note and locate the next one, if available
            chart_request_send(&fwe->transmit_chart, tx_ent);
            tx_ent = (struct transmit_chart_ent *) chart_request_start(&fwe->transmit_chart);
        }
    }
}
