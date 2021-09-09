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

static void *fakewire_exc_flowtx_loop(void *fwe_opaque);
static void *fakewire_exc_reader_loop(void *fwe_opaque);
static void fakewire_exc_reset(fw_exchange_t *fwe);

static void fakewire_exc_on_recv_data(void *opaque, uint8_t *bytes_in, size_t bytes_count);
static void fakewire_exc_on_recv_ctrl(void *opaque, fw_ctrl_t symbol, uint32_t param);

//#define DEBUG
//#define APIDEBUG

#define debug_puts(str) (debugf("[  fakewire_exc] [%s] %s", fwe->options.link_options.label, str))
#define debug_printf(fmt, ...) (debugf("[  fakewire_exc] [%s] " fmt, fwe->options.link_options.label, __VA_ARGS__))

static inline void fakewire_exc_check_invariants(fw_exchange_t *fwe) {
    assert(fwe->state >= FW_EXC_CONNECTING && fwe->state <= FW_EXC_OPERATING);
    assert(fwe->pkts_sent == fwe->fcts_rcvd || fwe->pkts_sent + 1 == fwe->fcts_rcvd);
}

int fakewire_exc_init(fw_exchange_t *fwe, fw_exchange_options_t opts) {
    memset(fwe, 0, sizeof(fw_exchange_t));
    mutex_init(&fwe->mutex);
    cond_init(&fwe->cond);
    mutex_init(&fwe->tx_busy);

    fwe->options = opts;
    fwe->link_interface = (fw_receiver_t) {
        .param = fwe,
        .recv_data = fakewire_exc_on_recv_data,
        .recv_ctrl = fakewire_exc_on_recv_ctrl,
    };

    assert(opts.recv_max_size >= 1);
    fwe->recv_buffer = malloc(opts.recv_max_size);
    assert(fwe->recv_buffer != NULL);

    fwe->recv_state = FW_RECV_PREPARING;
    fakewire_exc_reset(fwe);

    if (fakewire_link_init(&fwe->io_port, &fwe->link_interface, opts.link_options) < 0) {
        cond_destroy(&fwe->cond);
        mutex_destroy(&fwe->mutex);
        fwe->state = FW_EXC_INVALID;
        return -1;
    }

    thread_create(&fwe->flowtx_thread, "fw_flowtx_loop", PRIORITY_WORKERS, fakewire_exc_flowtx_loop, fwe);
    thread_create(&fwe->reader_thread, "fw_reader_loop", PRIORITY_SERVERS, fakewire_exc_reader_loop, fwe);
    return 0;
}

static void fakewire_exc_reset(fw_exchange_t *fwe) {
    fwe->state = FW_EXC_CONNECTING;
    if (fwe->recv_state != FW_RECV_CALLBACK) {
        fwe->recv_state = FW_RECV_PREPARING;
    }

    fwe->send_handshake_id = 0;
    fwe->send_secondary_handshake = false;
    fwe->recv_handshake_id = 0;

    fwe->fcts_sent = fwe->fcts_rcvd = fwe->pkts_sent = fwe->pkts_rcvd = 0;

    cond_broadcast(&fwe->cond);
}

static void fakewire_exc_on_recv_data(void *opaque, uint8_t *bytes_in, size_t bytes_count) {
    fw_exchange_t *fwe = (fw_exchange_t *) opaque;
    assert(fwe != NULL && bytes_in != NULL);
    assert(bytes_count > 0);

#ifdef DEBUG
    debug_printf("Received %zu regular data bytes.", bytes_count);
#endif

    mutex_lock(&fwe->mutex);
    fakewire_exc_check_invariants(fwe);

    if (fwe->state != FW_EXC_OPERATING) {
        assert(fwe->recv_state == FW_RECV_PREPARING || fwe->recv_state == FW_RECV_CALLBACK);
        debug_printf("Received unexpected data character 0x%x during handshake mode %d; resetting.", bytes_in[0], fwe->state);
        fakewire_exc_reset(fwe);
    } else if (fwe->recv_state == FW_RECV_OVERFLOWED) {
        // discard extraneous bytes and do nothing
    } else if (fwe->recv_state != FW_RECV_RECEIVING) {
        debug_printf("Hit unexpected data character 0x%x during receive state %d; resetting.", bytes_in[0], fwe->recv_state);
        fakewire_exc_reset(fwe);
    } else if (fwe->recv_offset + bytes_count > fwe->options.recv_max_size) {
        debug_printf("Packet exceeded buffer size %zu; discarding.", fwe->options.recv_max_size);
        fwe->recv_state = FW_RECV_OVERFLOWED;
    } else {
        // actually collect the received data and put it into the buffer
        assert(fwe->recv_buffer != NULL);
        assert(fwe->recv_offset < fwe->options.recv_max_size);

        memcpy(&fwe->recv_buffer[fwe->recv_offset], bytes_in, bytes_count);
        fwe->recv_offset += bytes_count;

        assert(fwe->recv_offset <= fwe->options.recv_max_size);
    }
    mutex_unlock(&fwe->mutex);
}

static void fakewire_exc_on_recv_ctrl(void *opaque, fw_ctrl_t symbol, uint32_t param) {
    fw_exchange_t *fwe = (fw_exchange_t *) opaque;
    assert(fwe != NULL);
    assert(param == 0 || fakewire_is_parametrized(symbol));

#ifdef DEBUG
    debug_printf("Received control character: %s(%u).", fakewire_codec_symbol(symbol), param);
#endif

    mutex_lock(&fwe->mutex);
    fakewire_exc_check_invariants(fwe);

    if (fwe->state == FW_EXC_CONNECTING) {
        switch (symbol) {
        case FWC_HANDSHAKE_1:
            // received a primary handshake
            debug_printf("Received a primary handshake with ID=0x%08x.", param);
            fwe->recv_handshake_id = param;
            fwe->send_secondary_handshake = true;
            cond_broadcast(&fwe->cond);
            break;
        case FWC_HANDSHAKE_2:
            debug_puts("Received unexpected secondary handshake when no primary handshake had been sent; resetting.");
            fakewire_exc_reset(fwe);
            break;
        default:
            debug_printf("Hit unexpected control character %s while CONNECTING; resetting.",
                         fakewire_codec_symbol(symbol));
            fakewire_exc_reset(fwe);
            break;
        }
    } else if (fwe->state == FW_EXC_HANDSHAKING) {
        switch (symbol) {
        case FWC_HANDSHAKE_1:
            debug_puts("Received primary handshake collision while handshaking; resetting.");
            fakewire_exc_reset(fwe);
            break;
        case FWC_HANDSHAKE_2:
            // received a secondary handshake
            if (param == fwe->send_handshake_id) {
                debug_printf("Received secondary handshake with ID=0x%08x; transitioning to operating mode.", param);
                fwe->state = FW_EXC_OPERATING;
                cond_broadcast(&fwe->cond);
            } else {
                debug_printf("Received mismatched secondary ID 0x%08x instead of 0x%08x; resetting.",
                             param, fwe->send_handshake_id);
                fakewire_exc_reset(fwe);
            }
            break;
        default:
            debug_printf("Hit unexpected control character %s while HANDSHAKING; resetting.",
                         fakewire_codec_symbol(symbol));
            fakewire_exc_reset(fwe);
            break;
        }
    } else if (fwe->state == FW_EXC_OPERATING) {
        switch (symbol) {
        case FWC_HANDSHAKE_1:
            // abort connection and restart everything
            debug_printf("Received primary handshake with ID=0x%08x during operating mode; resetting.", param);
            fakewire_exc_reset(fwe);

            // actually act on handshake
            fwe->recv_handshake_id = param;
            fwe->send_secondary_handshake = true;
            cond_broadcast(&fwe->cond);
            break;
        case FWC_HANDSHAKE_2:
            debug_puts("Received unexpected secondary handshake during operating mode; resetting.");
            fakewire_exc_reset(fwe);
            break;
        case FWC_START_PACKET:
            if (fwe->fcts_sent != fwe->pkts_rcvd + 1) {
                debug_printf("Received unauthorized start-of-packet (fcts_sent=%u, pkts_rcvd=%u); resetting.",
                             fwe->fcts_sent, fwe->pkts_rcvd);
                fakewire_exc_reset(fwe);
            } else {
                assert(fwe->recv_state == FW_RECV_LISTENING);
                fwe->recv_state = FW_RECV_RECEIVING;
                fwe->pkts_rcvd += 1;
                // reset receive buffer before proceeding
                memset(fwe->recv_buffer, 0, fwe->options.recv_max_size);
                fwe->recv_offset = 0;
            }
            break;
        case FWC_END_PACKET:
            if (fwe->recv_state == FW_RECV_OVERFLOWED) {
                // discard state and get ready for another packet
                fwe->recv_state = FW_RECV_PREPARING;
            } else if (fwe->recv_state == FW_RECV_RECEIVING) {
                // confirm completion
                fwe->recv_state = FW_RECV_CALLBACK;
                cond_broadcast(&fwe->cond);
            } else {
                debug_printf("Hit unexpected end-of-packet in receive state %d; resetting.", fwe->recv_state);
                fakewire_exc_reset(fwe);
            }
            break;
        case FWC_ERROR_PACKET:
            if (fwe->recv_state == FW_RECV_OVERFLOWED) {
                // discard state and get ready for another packet
                fwe->recv_state = FW_RECV_PREPARING;
            } else if (fwe->recv_state == FW_RECV_RECEIVING) {
                // discard state and get ready for another packet
                fwe->recv_state = FW_RECV_PREPARING;
            } else {
                debug_printf("Hit unexpected error-in-packet in receive state %d; resetting.", fwe->recv_state);
                fakewire_exc_reset(fwe);
            }
            break;
        case FWC_FLOW_CONTROL:
            if (param == fwe->fcts_rcvd + 1) {
                // make sure this FCT matches our send state
                if (fwe->pkts_sent != fwe->fcts_rcvd) {
                    debug_printf("Received incremented FCT(%u) when no packet had been sent (%u, %u); resetting.",
                                 param, fwe->pkts_sent, fwe->fcts_rcvd);
                    fakewire_exc_reset(fwe);
                } else {
                    // received FCT; can send another packet
                    fwe->fcts_rcvd = param;
                    cond_broadcast(&fwe->cond);
                }
            } else if (param != fwe->fcts_rcvd) {
                // FCT number should always either stay the same or increment by one.
                debug_printf("Received unexpected FCT(%u) when last count was %u; resetting.", param, fwe->fcts_rcvd);
                fakewire_exc_reset(fwe);
            }
            break;
        case FWC_KEEP_ALIVE:
            if (fwe->pkts_rcvd != param) {
                debug_printf("KAT mismatch: received %u packets, but supposed to have received %u; resetting.",
                             fwe->pkts_rcvd, param);
                fakewire_exc_reset(fwe);
            }
            break;
        case FWC_CODEC_ERROR:
            debug_puts("Received invalid escape sequence; resetting.");
            fakewire_exc_reset(fwe);
            break;
        default:
            assert(false);
        }
    } else {
        assert(false);
    }

    mutex_unlock(&fwe->mutex);
}

void fakewire_exc_write(fw_exchange_t *fwe, uint8_t *packet_in, size_t packet_len) {
    assert(fwe != NULL);

#ifdef APIDEBUG
    debug_printf("API write(%zu bytes) start", packet_len);
#endif

    mutex_lock(&fwe->mutex);
    // wait until handshake completes and transmit is possible
    while (true) {
        fakewire_exc_check_invariants(fwe);
        // check condition for write to proceed
        if (fwe->state == FW_EXC_OPERATING && fwe->pkts_sent != fwe->fcts_rcvd) {
            // now acquire the tx_busy mutex so that we can transmit
            if (!mutex_lock_try(&fwe->tx_busy)) {
#ifdef APIDEBUG
                debug_printf("API write(%zu bytes): WAIT ON TX_BUSY", packet_len);
#endif
                mutex_unlock(&fwe->mutex);
                mutex_lock(&fwe->tx_busy);
                mutex_lock(&fwe->mutex);
            }
            // since we may have released the mutex, we need to recheck the condition
            if (fwe->state == FW_EXC_OPERATING && fwe->pkts_sent != fwe->fcts_rcvd) {
                // great; we have the condition and the semaphore, so we can continue!
                break;
            }
            // if the condition is no longer true, give up the semaphore, and return around.
            mutex_unlock(&fwe->tx_busy);
        }
#ifdef APIDEBUG
        debug_printf("API write(%zu bytes): WAIT(state=%d, pkts_sent=%u, fcts_rcvd=%u)",
                    packet_len, fwe->state, fwe->pkts_sent, fwe->fcts_rcvd);
#endif
        cond_wait(&fwe->cond, &fwe->mutex);
    }

    assert(fwe->pkts_sent == fwe->fcts_rcvd - 1);
    fwe->pkts_sent += 1;

    mutex_unlock(&fwe->mutex);

    // perform actual transmit, with tx_busy mutex still held

    fw_receiver_t *link_write = fakewire_link_interface(&fwe->io_port);

    link_write->recv_ctrl(link_write->param, FWC_START_PACKET, 0);
    link_write->recv_data(link_write->param, packet_in, packet_len);
    link_write->recv_ctrl(link_write->param, FWC_END_PACKET, 0);

    // now give up tx_busy mutex to let another packet have its turn
    mutex_unlock(&fwe->tx_busy);

#ifdef APIDEBUG
    debug_printf("API write(%zu bytes) success", packet_len);
#endif
}

// random interval in the range [3ms, 10ms)
static uint64_t handshake_period(void) {
    uint64_t ms = 1000 * 1000;
    return (rand() % (7 * ms)) + 3 * ms;
}

static void *fakewire_exc_flowtx_loop(void *fwe_opaque) {
    fw_exchange_t *fwe = (fw_exchange_t *) fwe_opaque;
    assert(fwe != NULL);

    // tracking for both handshakes and heartbeats
    uint64_t next_interval = clock_timestamp_monotonic() + handshake_period();
    fw_receiver_t *link_write = fakewire_link_interface(&fwe->io_port);

    mutex_lock(&fwe->mutex);
    while (true) {
        fakewire_exc_check_invariants(fwe);

        uint64_t now = clock_timestamp_monotonic();

        if (fwe->send_secondary_handshake == false && now < next_interval
                && !(fwe->state == FW_EXC_OPERATING && fwe->recv_state == FW_RECV_PREPARING)) {
            // no conditions that require action. wait until one of them trips!

            cond_timedwait(&fwe->cond, &fwe->mutex, next_interval - now);
            // loop back around to recheck
            continue;
        }

        // we have a condition that requires activity... so acquire the tx_busy semaphore!
        if (!mutex_lock_try(&fwe->tx_busy)) {
            mutex_unlock(&fwe->mutex);
            mutex_lock(&fwe->tx_busy);
            mutex_lock(&fwe->mutex);
        }

        // if we received a primary handshake... then we need to send a secondary handshake in response
        // if we're handshaking... then we need to send primary handshakes on a regular basis
        // if we're ready to receive... then we need to send a FCT to permit the remote end to transmit
        // if we're operating... then we need to send FCTs and KATs on a regular basis

        if (fwe->send_secondary_handshake) {
            assert(fwe->state == FW_EXC_CONNECTING);
            uint32_t handshake_id = fwe->recv_handshake_id;

            link_write->recv_ctrl(link_write->param, FWC_HANDSHAKE_2, handshake_id);

            if (!fwe->send_secondary_handshake) {
                debug_printf("Sent secondary handshake with ID=0x%08x, but request revoked by reset; not transitioning.",
                             handshake_id);
            } else if (handshake_id != fwe->recv_handshake_id) {
                debug_printf("Sent secondary handshake with ID=0x%08x, but new primary ID=0x%08x had been received in the meantime; not transitioning.",
                             handshake_id, fwe->recv_handshake_id);
            } else if (fwe->state != FW_EXC_CONNECTING) {
                debug_printf("Sent secondary handshake with ID=0x%08x, but state is now %d instead of CONNECTING; not transitioning.",
                             handshake_id, fwe->state);
            } else {
                debug_printf("Sent secondary handshake with ID=0x%08x; transitioning to operating mode.",
                             handshake_id);
                fwe->state = FW_EXC_OPERATING;
                fwe->send_secondary_handshake = false;
            }

            cond_broadcast(&fwe->cond);

            now = clock_timestamp_monotonic();
            next_interval = now + handshake_period();
        } else if (fwe->state == FW_EXC_OPERATING &&
                       (now >= next_interval || fwe->recv_state == FW_RECV_PREPARING)) {
            if (fwe->recv_state == FW_RECV_PREPARING) {
#ifdef DEBUG
                debug_puts("Sending FCT.");
#endif
                fwe->fcts_sent += 1;
                fwe->recv_state = FW_RECV_LISTENING;
            }
#ifdef DEBUG
            debug_printf("Transmitting reminder FCT(%u) and KAT(%u) tokens.",
                         fwe->fcts_sent, fwe->pkts_sent);
#endif
            link_write->recv_ctrl(link_write->param, FWC_FLOW_CONTROL, fwe->fcts_sent);
            link_write->recv_ctrl(link_write->param, FWC_KEEP_ALIVE, fwe->pkts_sent);
            cond_broadcast(&fwe->cond);

            now = clock_timestamp_monotonic();
            next_interval = now + handshake_period();
        } else if (now >= next_interval) {
            assert(fwe->state == FW_EXC_HANDSHAKING || fwe->state == FW_EXC_CONNECTING);
            // pick something very likely to be distinct (Go picks msb unset, C picks msb set)
            uint32_t handshake_id = 0x80000000 + (0x7FFFFFFF & (uint32_t) clock_timestamp_monotonic());
            debug_printf("Timeout expired; attempting primary handshake with ID=0x%08x; transitioning to handshaking mode.",
                         handshake_id);
            fwe->send_handshake_id = handshake_id;
            fwe->state = FW_EXC_HANDSHAKING;

            link_write->recv_ctrl(link_write->param, FWC_HANDSHAKE_1, handshake_id);

            debug_printf("Sent primary handshake with ID=0x%08x.", handshake_id);

            cond_broadcast(&fwe->cond);

            now = clock_timestamp_monotonic();
            next_interval = now + handshake_period();
        } else {
#ifdef DEBUG
            debug_puts("Spurious wakeup; condition justifying tx_busy no longer applies.");
#endif
        }

        // return the semaphore back
        mutex_unlock(&fwe->tx_busy);
    }
}

static void *fakewire_exc_reader_loop(void *fwe_opaque) {
    fw_exchange_t *fwe = (fw_exchange_t *) fwe_opaque;
    assert(fwe != NULL);
    assert(fwe->recv_buffer != NULL);

    mutex_lock(&fwe->mutex);
    while (true) {
        fakewire_exc_check_invariants(fwe);

        // wait until we have a callback to handle
        if (fwe->recv_state != FW_RECV_CALLBACK) {
            cond_wait(&fwe->cond, &fwe->mutex);
            continue;
        }

        assert(fwe->recv_offset <= fwe->options.recv_max_size);

        mutex_unlock(&fwe->mutex);

#ifdef APIDEBUG
        debug_printf("API callback for read(%zd bytes/%zu bytes) starting...", fwe->recv_offset, fwe->options.recv_max_size);
#endif
        fwe->options.recv_callback(fwe->options.recv_param, fwe->recv_buffer, fwe->recv_offset);
#ifdef APIDEBUG
        debug_puts("API callback for read completed.");
#endif

        mutex_lock(&fwe->mutex);

        assert(fwe->recv_state == FW_RECV_CALLBACK);
        fwe->recv_state = FW_RECV_PREPARING;
        cond_broadcast(&fwe->cond);
    }
}
