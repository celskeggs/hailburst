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
static void fakewire_exc_on_recv_ctrl(void *opaque, fw_ctrl_t symbol, uint32_t param);

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
        INPUT_TXMIT_COMPLETE,
    } type;
    union {
        struct {
            fw_ctrl_t symbol;
            uint32_t  param;
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
        /* no parameters on txmit_complete */
    };
};

struct transmit_queue_ent {
    fw_ctrl_t symbol; // if FWC_NONE, indicates a data character entry
    union {
        uint32_t ctrl_param;
        struct {
            uint8_t *data_ptr;
            size_t   data_len;
        } data_param;
    };
};

struct read_cb_queue_ent {
    /* buffer pointer not necessary, because it's always 'recv_buffer' in fw_exchange_t */
    size_t read_size;
};

int fakewire_exc_init(fw_exchange_t *fwe, fw_exchange_options_t opts) {
    memset(fwe, 0, sizeof(fw_exchange_t));
    queue_init(&fwe->input_queue, sizeof(struct input_queue_ent), 16);
    queue_init(&fwe->transmit_queue, sizeof(struct transmit_queue_ent), 1); // TODO: could we have more than one in flight at once?
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

    if (fakewire_link_init(&fwe->io_port, &fwe->link_interface, opts.link_options) < 0) {
        free(fwe->recv_buffer);
        semaphore_destroy(&fwe->write_ready_sem);
        queue_destroy(&fwe->read_cb_queue);
        queue_destroy(&fwe->transmit_queue);
        queue_destroy(&fwe->input_queue);
        return -1;
    }

    thread_create(&fwe->exchange_thread, "fw_exc_thread",      PRIORITY_SERVERS, fakewire_exc_exchange_loop, fwe);
    thread_create(&fwe->read_cb_thread,  "fw_read_cb_thread",  PRIORITY_SERVERS, fakewire_exc_read_cb_loop,  fwe);
    thread_create(&fwe->transmit_thread, "fw_transmit_thread", PRIORITY_SERVERS, fakewire_exc_transmit_loop, fwe);
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

static void fakewire_exc_on_recv_ctrl(void *opaque, fw_ctrl_t symbol, uint32_t param) {
    fw_exchange_t *fwe = (fw_exchange_t *) opaque;
    assert(fwe != NULL && fakewire_is_special(symbol));
    assert(param == 0 || fakewire_is_parametrized(symbol));

    struct input_queue_ent entry = {
        .type = INPUT_RECV_CTRL_CHAR,
        .ctrl_char = {
            .symbol = symbol,
            .param  = param,
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
        fwe->options.recv_callback(fwe->options.recv_param, fwe->recv_buffer, read_cb_entry.read_size);
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

    fw_receiver_t *link_write = fakewire_link_interface(&fwe->io_port);
    struct transmit_queue_ent txmit_entry;

    while (true) {
        // wait for something to transmit
        queue_recv(&fwe->transmit_queue, &txmit_entry);

        // dispatch callback
        if (txmit_entry.symbol == FWC_NONE) {
#ifdef DEBUG
            debug_printf("Transmitting %zu data characters.", txmit_entry.data_param.data_len);
#endif
            // data characters
            link_write->recv_data(link_write->param, txmit_entry.data_param.data_ptr, txmit_entry.data_param.data_len);
        } else {
#ifdef DEBUG
            debug_printf("Transmitting control character %s(0x%08x).", fakewire_codec_symbol(txmit_entry.symbol), txmit_entry.ctrl_param);
#endif
            // control character
            link_write->recv_ctrl(link_write->param, txmit_entry.symbol, txmit_entry.ctrl_param);
        }

        // send transmit complete notification
        struct input_queue_ent entry = {
            .type = INPUT_TXMIT_COMPLETE,
        };
        queue_send(&fwe->input_queue, &entry);
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

    size_t recv_offset = 0;

    uint8_t *cur_packet_in     = NULL;
    size_t   cur_packet_len    = 0;
    wakeup_t cur_packet_wakeup = NULL;

    bool can_transmit = true;

    struct input_queue_ent input_ent;

    // make sure we accept input from the first writer
    semaphore_give(&fwe->write_ready_sem);

    while (true) {
#ifdef DEBUG
        debug_printf("Entering main exchange loop (can_transmit=%s).", can_transmit ? "true" : "false");
#endif
        // event loop centered around the input queue... this should be the ONLY blocking call in this thread!
        bool timed_out = false;
        if (can_transmit) {
            timed_out = !queue_recv_timed_abs(&fwe->input_queue, &input_ent, next_timeout);
        } else {
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
            case INPUT_TXMIT_COMPLETE:
                wakeup_explanation = "INPUT_TXMIT_COMPLETE";
                break;
            }
        }
        debug_printf("Woke up main exchange loop (%s)", wakeup_explanation);
#endif

        // check invariants
        assert(exc_state >= FW_EXC_CONNECTING && exc_state <= FW_EXC_OPERATING);
        assert(pkts_sent == fcts_rcvd || pkts_sent + 1 == fcts_rcvd);

        bool do_reset = false;

        if (timed_out) {
            assert(clock_timestamp_monotonic() >= next_timeout);
            assert(can_transmit == true);

            if (exc_state == FW_EXC_OPERATING) {
                resend_fcts = true;
                resend_pkts = true;
            } else {
                assert(exc_state == FW_EXC_HANDSHAKING || exc_state == FW_EXC_CONNECTING);
                // pick something very likely to be distinct (Go picks msb unset, C picks msb set)
                send_handshake_id = 0x80000000 + (0x7FFFFFFF & (uint32_t) clock_timestamp_monotonic());
                debug_printf("Timeout expired; attempting primary handshake with ID=0x%08x; transitioning to handshaking mode.",
                             send_handshake_id);
                exc_state = FW_EXC_HANDSHAKING;

                struct transmit_queue_ent tx_ent = {
                    .symbol     = FWC_HANDSHAKE_1,
                    .ctrl_param = send_handshake_id,
                };
                bool sent = queue_send_try(&fwe->transmit_queue, &tx_ent);
                assert(sent == true);
                can_transmit = false;

                debug_printf("Sent primary handshake with ID=0x%08x.", send_handshake_id);
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
                            .read_size = recv_offset,
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
        } else if (input_ent.type == INPUT_TXMIT_COMPLETE) {
            can_transmit = true;
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

        if (can_transmit && resend_fcts) {
            assert(exc_state == FW_EXC_OPERATING);
#ifdef DEBUG
            debug_printf("Transmitting reminder FCT(%u) tokens.", fcts_sent);
#endif
            struct transmit_queue_ent tx_ent = {
                .symbol     = FWC_FLOW_CONTROL,
                .ctrl_param = fcts_sent,
            };
            bool sent = queue_send_try(&fwe->transmit_queue, &tx_ent);
            assert(sent == true);
            can_transmit = false;
            resend_fcts = false;
        }

        if (can_transmit && resend_pkts) {
            assert(exc_state == FW_EXC_OPERATING);
#ifdef DEBUG
            debug_printf("Transmitting reminder KAT(%u) tokens.", pkts_sent);
#endif
            struct transmit_queue_ent tx_ent = {
                .symbol     = FWC_KEEP_ALIVE,
                .ctrl_param = pkts_sent,
            };
            bool sent = queue_send_try(&fwe->transmit_queue, &tx_ent);
            assert(sent == true);
            can_transmit = false;
            resend_pkts = false;
        }

        if (can_transmit && send_secondary_handshake) {
            assert(exc_state == FW_EXC_CONNECTING);
            struct transmit_queue_ent tx_ent = {
                .symbol     = FWC_HANDSHAKE_2,
                .ctrl_param = recv_handshake_id,
            };
            bool sent = queue_send_try(&fwe->transmit_queue, &tx_ent);
            assert(sent == true);
            can_transmit = false;

            debug_printf("Sent secondary handshake with ID=0x%08x; transitioning to operating mode.",
                         recv_handshake_id);
            exc_state = FW_EXC_OPERATING;
            send_secondary_handshake = false;

            next_timeout = clock_timestamp_monotonic() + handshake_period();
        }

        if (can_transmit && exc_state == FW_EXC_OPERATING && txmit_state != FW_TXMIT_IDLE) {
            assert(cur_packet_in != NULL);

            bool do_transmit = true;
            struct transmit_queue_ent tx_ent;
            switch (txmit_state) {
            case FW_TXMIT_HEADER:
                if (fcts_rcvd == pkts_sent) {
                    // cannot transmit header yet
                    do_transmit = false;
                    break;
                }
                tx_ent.symbol = FWC_START_PACKET;
                tx_ent.ctrl_param = 0;
                txmit_state = FW_TXMIT_BODY;
                pkts_sent++;
                break;
            case FW_TXMIT_BODY:
                tx_ent.symbol = FWC_NONE;
                tx_ent.data_param.data_ptr = cur_packet_in;
                tx_ent.data_param.data_len = cur_packet_len;
                txmit_state = FW_TXMIT_FOOTER;
                break;
            case FW_TXMIT_FOOTER:
                // transmit end-of-packet character
                tx_ent.symbol = FWC_END_PACKET;
                tx_ent.ctrl_param = 0;

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
                break;
            default:
                assert(false);
            }

            if (do_transmit) {
                bool sent = queue_send_try(&fwe->transmit_queue, &tx_ent);
                assert(sent == true);
                can_transmit = false;
            }
        }
    }
}
