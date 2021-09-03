#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "clock.h"
#include "debug.h"
#include "fakewire_exc.h"
#include "thread.h"

static void *fakewire_exc_flowtx_loop(void *fwe_opaque);

static void fakewire_exc_on_recv_data(void *opaque, uint8_t *bytes_in, size_t bytes_count);
static void fakewire_exc_on_recv_ctrl(void *opaque, fw_ctrl_t symbol, uint32_t param);

//#define DEBUG
//#define APIDEBUG

#define debug_puts(str) (debugf("[  fakewire_exc] [%s] %s", fwe->label, str))
#define debug_printf(fmt, ...) (debugf("[  fakewire_exc] [%s] " fmt, fwe->label, __VA_ARGS__))

static inline void fakewire_exc_check_invariants(fw_exchange_t *fwe) {
    assert(fwe->state >= FW_EXC_DISCONNECTED && fwe->state <= FW_EXC_OPERATING);
    assert(fwe->pkts_sent == fwe->fcts_rcvd || fwe->pkts_sent + 1 == fwe->fcts_rcvd);
}

void fakewire_exc_init(fw_exchange_t *fwe, const char *label) {
    memset(fwe, 0, sizeof(fw_exchange_t));
    fwe->state = FW_EXC_DISCONNECTED;
    fwe->label = label;

    fwe->link_interface = (fw_receiver_t) {
        .param = fwe,
        .recv_data = fakewire_exc_on_recv_data,
        .recv_ctrl = fakewire_exc_on_recv_ctrl,
    };

    mutex_init(&fwe->mutex);
    cond_init(&fwe->cond);
}

void fakewire_exc_destroy(fw_exchange_t *fwe) {
    assert(fwe->state == FW_EXC_DISCONNECTED);

    cond_destroy(&fwe->cond);
    mutex_destroy(&fwe->mutex);

    memset(fwe, 0, sizeof(fw_exchange_t));
    fwe->state = FW_EXC_INVALID;
}

static void fakewire_exc_reset(fw_exchange_t *fwe) {
    fwe->state = FW_EXC_CONNECTING;

    fwe->send_handshake_id = 0;
    fwe->send_secondary_handshake = false;
    fwe->recv_handshake_id = 0;

    fwe->fcts_sent = fwe->fcts_rcvd = fwe->pkts_sent = fwe->pkts_rcvd = 0;

    fwe->inbound_buffer = NULL;
    fwe->inbound_read_done = false;
    fwe->recv_in_progress = false;

    cond_broadcast(&fwe->cond);
}

int fakewire_exc_attach(fw_exchange_t *fwe, const char *path, int flags) {
    mutex_lock(&fwe->mutex);
    assert(fwe->state == FW_EXC_DISCONNECTED);
    assert(!fwe->detaching);

    if (fakewire_link_init(&fwe->io_port, &fwe->link_interface, path, flags, fwe->label) < 0) {
        mutex_unlock(&fwe->mutex);
        return -1;
    }
    fakewire_exc_reset(fwe);

    thread_create(&fwe->flowtx_thread, fakewire_exc_flowtx_loop, fwe);
    mutex_unlock(&fwe->mutex);
    return 0;
}

#ifndef __FREERTOS__
void fakewire_exc_detach(fw_exchange_t *fwe) {
    // acquire lock and check assumptions
#ifdef DEBUG
    debug_puts("Acquiring lock to tear down exchange...");
#endif
    mutex_lock(&fwe->mutex);
#ifdef DEBUG
    debug_puts("Acquired lock.");
#endif
    fakewire_exc_check_invariants(fwe);

    assert(fwe->state != FW_EXC_DISCONNECTED);
    assert(!fwe->detaching);
    thread_t flowtx_thread = fwe->flowtx_thread;

    // set state to cause teardown
    fwe->state = FW_EXC_DISCONNECTED;
    fwe->detaching = true;
    cond_broadcast(&fwe->cond);

    fakewire_link_shutdown(&fwe->io_port);

    // wait until flowtx thread terminates
    mutex_unlock(&fwe->mutex);
#ifdef DEBUG
    debug_puts("Tearing down flowtx_thread...");
#endif
    thread_join(flowtx_thread);
#ifdef DEBUG
    debug_puts("Tore down flowtx_thread.");
#endif
    mutex_lock(&fwe->mutex);

    // clear flowtx thread handle
    assert(fwe->flowtx_thread == flowtx_thread);

    // wait until all transmissions complete
#ifdef DEBUG
    debug_puts("Waiting for transmissions to complete...");
#endif
    while (fwe->tx_busy) {
        cond_wait(&fwe->cond, &fwe->mutex);
    }
#ifdef DEBUG
    debug_puts("All transmissions completed.");
#endif

    // tear down I/O port
    fakewire_link_destroy(&fwe->io_port);

    // clean up detach state
    assert(fwe->state == FW_EXC_DISCONNECTED);
    assert(fwe->detaching == true);
    fwe->detaching = false;
    mutex_unlock(&fwe->mutex);
}
#endif

static void fakewire_exc_on_recv_data(void *opaque, uint8_t *bytes_in, size_t bytes_count) {
    fw_exchange_t *fwe = (fw_exchange_t *) opaque;
    assert(fwe != NULL && bytes_in != NULL);
    assert(bytes_count > 0);

#ifdef DEBUG
    debug_printf("Received %zu regular data bytes.", bytes_count);
#endif

    mutex_lock(&fwe->mutex);
    fakewire_exc_check_invariants(fwe);

    if (fwe->state == FW_EXC_DISCONNECTED) {
        // ignore data characters; do nothing
    } else if (fwe->state == FW_EXC_OPERATING) {
        if (!fwe->recv_in_progress) {
            debug_printf("Hit unexpected data character 0x%x before start-of-packet; resetting.", bytes_in[0]);
            fakewire_exc_reset(fwe);
            mutex_unlock(&fwe->mutex);
            return;
        }
        assert(fwe->inbound_buffer != NULL);
        assert(!fwe->inbound_read_done);
        assert(fwe->inbound_buffer_offset <= fwe->inbound_buffer_max);

        size_t copy_n = fwe->inbound_buffer_max - fwe->inbound_buffer_offset;
        if (copy_n > bytes_count) {
            copy_n = bytes_count;
        }
        if (copy_n > 0) {
            memcpy(&fwe->inbound_buffer[fwe->inbound_buffer_offset], bytes_in, copy_n);
        }
        // keep incrementing even if we overflow so that the reader can tell that the packet was truncated
        fwe->inbound_buffer_offset += bytes_count;
    } else {
        assert(fwe->inbound_buffer == NULL);
        debug_printf("Received unexpected data character 0x%x during handshake mode %d; resetting.", bytes_in[0], fwe->state);
        fakewire_exc_reset(fwe);
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

    if (fwe->state == FW_EXC_DISCONNECTED) {
        // ignore control character
    } else if (fwe->state == FW_EXC_CONNECTING) {
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
        case FWC_START_PACKET: // fallthrough
        case FWC_END_PACKET:   // fallthrough
        case FWC_ERROR_PACKET: // fallthrough
        case FWC_FLOW_CONTROL: // fallthrough
        case FWC_KEEP_ALIVE:   // fallthrough
        case FWC_CODEC_ERROR:
            debug_printf("Hit unexpected control character %s while CONNECTING; resetting.",
                         fakewire_codec_symbol(symbol));
            fakewire_exc_reset(fwe);
            break;
        default:
            assert(false);
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
        case FWC_START_PACKET: // fallthrough
        case FWC_END_PACKET:   // fallthrough
        case FWC_ERROR_PACKET: // fallthrough
        case FWC_FLOW_CONTROL: // fallthrough
        case FWC_KEEP_ALIVE:   // fallthrough
        case FWC_CODEC_ERROR:
            debug_printf("Hit unexpected control character %s while HANDSHAKING; resetting.",
                         fakewire_codec_symbol(symbol));
            fakewire_exc_reset(fwe);
            break;
        default:
            assert(false);
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
                assert(fwe->inbound_buffer != NULL); // should always have a buffer if we sent a FCT!
                assert(!fwe->inbound_read_done);     // if done hasn't been reset to false, we shouldn't have sent a FCT!
                assert(!fwe->recv_in_progress);

                fwe->recv_in_progress = true;
                fwe->pkts_rcvd += 1;
            }
            break;
        case FWC_END_PACKET:
            if (!fwe->recv_in_progress) {
                debug_puts("Hit unexpected end-of-packet before start-of-packet; resetting.");
                fakewire_exc_reset(fwe);
            } else {
                assert(fwe->inbound_buffer != NULL); // should always have a buffer if a read is in progress!
                assert(!fwe->inbound_read_done);

                // confirm completion
                fwe->inbound_read_done = true;
                fwe->recv_in_progress = false;
                cond_broadcast(&fwe->cond);
            }
            break;
        case FWC_ERROR_PACKET:
            if (!fwe->recv_in_progress) {
                debug_puts("Hit unexpected error-end-of-packet before start-of-packet; resetting.");
                fakewire_exc_reset(fwe);
            } else {
                assert(fwe->inbound_buffer != NULL); // should always have a buffer if a read is in progress!
                assert(!fwe->inbound_read_done);
                // discard the data in the current packet
                fwe->inbound_buffer_offset = 0;
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

ssize_t fakewire_exc_read(fw_exchange_t *fwe, uint8_t *packet_out, size_t packet_max) {
    assert(fwe != NULL);

#ifdef APIDEBUG
    debug_printf("API read(%zu bytes) start", packet_max);
#endif

    ssize_t actual_len = -1;

    mutex_lock(&fwe->mutex);
    while (fwe->state != FW_EXC_DISCONNECTED) {
        fakewire_exc_check_invariants(fwe);

        // wait until handshake completes and receive is possible
        if (fwe->state != FW_EXC_OPERATING || fwe->inbound_buffer != NULL) {
            cond_wait(&fwe->cond, &fwe->mutex);
            continue;
        }

#ifdef DEBUG
        debug_puts("Registering inbound buffer for receive...");
#endif

        // make sure packet is clear
        memset(packet_out, 0, packet_max);
        // set up receive buffers
        assert(!fwe->recv_in_progress);
        assert(fwe->pkts_rcvd == fwe->fcts_sent);
        fwe->inbound_buffer = packet_out;
        fwe->inbound_buffer_offset = 0;
        fwe->inbound_buffer_max = packet_max;
        fwe->inbound_read_done = false;
        cond_broadcast(&fwe->cond);

        while (!fwe->inbound_read_done && fwe->state == FW_EXC_OPERATING && fwe->inbound_buffer == packet_out) {
            cond_wait(&fwe->cond, &fwe->mutex);
        }
        if (fwe->state == FW_EXC_OPERATING && fwe->inbound_buffer == packet_out) {
            assert(fwe->inbound_read_done == true);
            assert(fwe->inbound_buffer_max == packet_max);
            fwe->inbound_buffer = NULL;
            fwe->inbound_read_done = false;
            cond_broadcast(&fwe->cond);

            actual_len = fwe->inbound_buffer_offset;
            assert(actual_len >= 0);
            break;
        }

        // the connection must have gotten reset... let's try again
    }
    mutex_unlock(&fwe->mutex);

#ifdef APIDEBUG
    if (actual_len >= 0) {
        debug_printf("API read(%zu bytes) success(%zd bytes)", packet_max, actual_len);
    } else {
        debug_printf("API read(%zu bytes) ERROR: disconnected", packet_max);
    }
#endif

    return actual_len;
}

int fakewire_exc_write(fw_exchange_t *fwe, uint8_t *packet_in, size_t packet_len) {
    assert(fwe != NULL);

#ifdef APIDEBUG
    debug_printf("API write(%zu bytes) start", packet_len);
#endif

    mutex_lock(&fwe->mutex);
    // wait until handshake completes and transmit is possible
    while (fwe->state != FW_EXC_OPERATING || fwe->pkts_sent == fwe->fcts_rcvd || fwe->tx_busy) {
        fakewire_exc_check_invariants(fwe);

        if (fwe->state == FW_EXC_DISCONNECTED) {
            mutex_unlock(&fwe->mutex);

#ifdef APIDEBUG
            debug_printf("API write(%zu bytes) ERROR: disconnected", packet_len);
#endif
            return -1;
        }

#ifdef APIDEBUG
        debug_printf("API write(%zu bytes): WAIT(state=%d, pkts_sent=%u, fcts_rcvd=%u, busy=%d)",
                     packet_len, fwe->state, fwe->pkts_sent, fwe->fcts_rcvd, fwe->tx_busy);
#endif
        cond_wait(&fwe->cond, &fwe->mutex);
    }

    assert(fwe->tx_busy == false);
    assert(fwe->pkts_sent == fwe->fcts_rcvd - 1);
    fwe->tx_busy = true;
    fwe->pkts_sent += 1;

    mutex_unlock(&fwe->mutex);

    // now actual transmit

    fw_receiver_t *link_write = fakewire_link_interface(&fwe->io_port);

    link_write->recv_ctrl(link_write->param, FWC_START_PACKET, 0);
    link_write->recv_data(link_write->param, packet_in, packet_len);
    link_write->recv_ctrl(link_write->param, FWC_END_PACKET, 0);

    // now let another packet have its turn
    mutex_lock(&fwe->mutex);
    assert(fwe->tx_busy == true);
    fwe->tx_busy = false;
    cond_broadcast(&fwe->cond);
    mutex_unlock(&fwe->mutex);

#ifdef APIDEBUG
    debug_printf("API write(%zu bytes) success", packet_len);
#endif

    return 0;
}

// random interval in the range [3ms, 10ms)
static uint64_t handshake_period(void) {
    uint64_t ms = 1000 * 1000;
    return (rand() % (7 * ms)) + 3 * ms;
}

static void fakewire_exc_send_ctrl_char(fw_exchange_t *fwe, fw_ctrl_t ctrl, uint32_t param) {
    assert(fwe->tx_busy == false);
    fwe->tx_busy = true;
    mutex_unlock(&fwe->mutex);

    fw_receiver_t *link_write = fakewire_link_interface(&fwe->io_port);

    link_write->recv_ctrl(link_write->param, ctrl, param);

    mutex_lock(&fwe->mutex);
    assert(fwe->tx_busy == true);
    fwe->tx_busy = false;
}

static void *fakewire_exc_flowtx_loop(void *fwe_opaque) {
    fw_exchange_t *fwe = (fw_exchange_t *) fwe_opaque;
    assert(fwe != NULL);

    // tracking for both handshakes and heartbeats
    uint64_t next_interval = clock_timestamp_monotonic() + handshake_period();

    mutex_lock(&fwe->mutex);
    while (fwe->state != FW_EXC_DISCONNECTED) {
        fakewire_exc_check_invariants(fwe);

        uint64_t bound_ns = 0;

        if (!fwe->tx_busy) {
            // if we received a primary handshake... then we need to send a secondary handshake in response
            // if we're handshaking... then we need to send primary handshakes on a regular basis
            // if we're ready to receive... then we need to send a FCT to permit the remote end to transmit
            // if we're operating... then we need to send FCTs and KATs on a regular basis
            uint64_t now = clock_timestamp_monotonic();

            if (fwe->send_secondary_handshake) {
                assert(fwe->state == FW_EXC_CONNECTING);
                uint32_t handshake_id = fwe->recv_handshake_id;

                fakewire_exc_send_ctrl_char(fwe, FWC_HANDSHAKE_2, handshake_id);

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
            } else if (now >= next_interval && fwe->state != FW_EXC_OPERATING) {
                assert(fwe->state == FW_EXC_HANDSHAKING || fwe->state == FW_EXC_CONNECTING);
                // pick something very likely to be distinct (Go picks msb unset, C picks msb set)
                uint32_t handshake_id = 0x80000000 + (0x7FFFFFFF & (uint32_t) clock_timestamp_monotonic());
                debug_printf("Timeout expired; attempting primary handshake with ID=0x%08x; transitioning to handshaking mode.",
                             handshake_id);
                fwe->send_handshake_id = handshake_id;
                fwe->state = FW_EXC_HANDSHAKING;

                fakewire_exc_send_ctrl_char(fwe, FWC_HANDSHAKE_1, handshake_id);

                debug_printf("Sent primary handshake with ID=0x%08x.", handshake_id);

                cond_broadcast(&fwe->cond);

                now = clock_timestamp_monotonic();
                next_interval = now + handshake_period();
            } else if (fwe->state == FW_EXC_OPERATING && fwe->inbound_buffer != NULL
                        && fwe->fcts_sent == fwe->pkts_rcvd && !fwe->recv_in_progress && !fwe->inbound_read_done) {
                // if we're ready to receive data, but haven't sent a FCT, send one
                fwe->fcts_sent += 1;
#ifdef DEBUG
                debug_printf("Sending FCT(%u) and KAT(%u) on inbound_buffer=%p (fwe=%p)", fwe->fcts_sent, fwe->pkts_sent, fwe->inbound_buffer, fwe);
#endif
                fakewire_exc_send_ctrl_char(fwe, FWC_FLOW_CONTROL, fwe->fcts_sent);
                fakewire_exc_send_ctrl_char(fwe, FWC_KEEP_ALIVE, fwe->pkts_sent);
                cond_broadcast(&fwe->cond);

                now = clock_timestamp_monotonic();
                next_interval = now + handshake_period();
            } else if (now >= next_interval) {
                assert(fwe->state == FW_EXC_OPERATING);
#ifdef DEBUG
                debug_printf("Interval expired: transmitting reminder FCT(%u) and KAT(%u) tokens.",
                             fwe->fcts_sent, fwe->pkts_sent);
#endif
                fakewire_exc_send_ctrl_char(fwe, FWC_FLOW_CONTROL, fwe->fcts_sent);
                fakewire_exc_send_ctrl_char(fwe, FWC_KEEP_ALIVE, fwe->pkts_sent);
                cond_broadcast(&fwe->cond);

                now = clock_timestamp_monotonic();
                next_interval = now + handshake_period();
            }

            if (now < next_interval) {
                bound_ns = next_interval - now;
            }
        }

        if (bound_ns) {
            cond_timedwait(&fwe->cond, &fwe->mutex, bound_ns);
        } else {
            cond_wait(&fwe->cond, &fwe->mutex);
        }
    }
    mutex_unlock(&fwe->mutex);
    return NULL;
}
