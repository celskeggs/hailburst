#include <arpa/inet.h>
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
static void fakewire_exc_on_recv_ctrl(void *opaque, fw_ctrl_t symbol);

//#define DEBUG

#define debug_puts(str) (debugf("[  fakewire_exc] [%s] %s", fwe->label, str))
#define debug_printf(fmt, ...) (debugf("[  fakewire_exc] [%s] " fmt, fwe->label, __VA_ARGS__))

static inline void fakewire_exc_check_invariants(fw_exchange_t *fwe) {
    assert(fwe->state >= FW_EXC_DISCONNECTED && fwe->state <= FW_EXC_OPERATING);
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
    fwe->state = FW_EXC_HANDSHAKING;

    fwe->sent_primary_handshake = false;
    fwe->needs_send_secondary_handshake = false;
    fwe->primary_id = fwe->secondary_id = 0;
    fwe->recvid_state = FW_RID_IDLE;
    fwe->recvid_offset = 0;

    fwe->inbound_buffer = NULL;
    fwe->inbound_read_done = false;
    fwe->has_sent_fct = false;
    fwe->remote_sent_fct = false;
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
    fwe->has_last_sent_primary_id = false;
    fwe->last_sent_primary_id = 0;
    fakewire_exc_reset(fwe);

    thread_create(&fwe->flowtx_thread, fakewire_exc_flowtx_loop, fwe);
    mutex_unlock(&fwe->mutex);
    return 0;
}

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
    pthread_t flowtx_thread = fwe->flowtx_thread;

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
    } else if (fwe->recvid_state == FW_RID_PRIMARY_ID || fwe->recvid_state == FW_RID_SECONDARY_ID) {
        assert(fwe->state == FW_EXC_HANDSHAKING ||
                   (fwe->recvid_state == FW_RID_SECONDARY_ID && fwe->state == FW_EXC_OPERATING));
        assert(fwe->recvid_offset < sizeof(uint32_t));
        if (bytes_count > sizeof(uint32_t) - fwe->recvid_offset) {
            debug_puts("Received too many data characters during handshake; resetting.");
            fakewire_exc_reset(fwe);
            mutex_unlock(&fwe->mutex);
            return;
        }
        uint8_t *out = (fwe->recvid_state == FW_RID_PRIMARY_ID ? (uint8_t*) &fwe->primary_id : (uint8_t*) &fwe->secondary_id);
        memcpy(out + fwe->recvid_offset, bytes_in, bytes_count);
        fwe->recvid_offset += bytes_count;
        assert(fwe->recvid_offset <= sizeof(uint32_t));

        if (fwe->recvid_offset == sizeof(uint32_t)) {
            if (fwe->recvid_state == FW_RID_PRIMARY_ID) {
                // primary handshake
                if (fwe->has_last_sent_primary_id && fwe->primary_id == fwe->last_sent_primary_id) {
                    debug_printf("Received the same handshake ID that we sent: 0x%08x; ignoring. (Is there a loop?)", ntohl(fwe->primary_id));
                } else {
                    debug_printf("Received primary handshake with ID=0x%08x.", ntohl(fwe->primary_id));

                    // have the flowtx thread transmit a secondary handshake
                    fwe->needs_send_secondary_handshake = true;
                    cond_broadcast(&fwe->cond);
                }
            } else if (!fwe->has_last_sent_primary_id) {
                debug_printf("Received secondary handshake with ID=0x%08x when primary had never been sent; resetting.",
                             ntohl(fwe->secondary_id));
                fakewire_exc_reset(fwe);
            } else if (fwe->state == FW_EXC_HANDSHAKING && !fwe->sent_primary_handshake) {
                // we don't want this check when we're in OPERATING mode, because it could be about us receiving a much
                // older message.
                debug_printf("Received secondary handshake with ID=0x%08x when primary had not been sent; resetting.",
                             ntohl(fwe->secondary_id));
                fakewire_exc_reset(fwe);
            } else if (fwe->last_sent_primary_id != fwe->secondary_id) {
                debug_printf("Received mismatched secondary ID 0x%08x instead of 0x%08x; resetting.",
                             ntohl(fwe->secondary_id), ntohl(fwe->last_sent_primary_id));
                fakewire_exc_reset(fwe);
            } else if (fwe->state == FW_EXC_HANDSHAKING) {
                debug_printf("Received secondary handshake with ID=0x%08x; transitioning to operating mode.",
                             ntohl(fwe->secondary_id));
                fwe->state = FW_EXC_OPERATING;
                cond_broadcast(&fwe->cond);
            } else {
                assert(fwe->state == FW_EXC_OPERATING);
                debug_printf("Received unnecessary secondary handshake with ID=0x%08x; ignoring.",
                             ntohl(fwe->secondary_id));
            }
            fwe->recvid_state = FW_RID_IDLE;
        }
    } else if (fwe->state == FW_EXC_HANDSHAKING) {
        assert(fwe->recvid_state == FW_RID_IDLE);
        assert(fwe->inbound_buffer == NULL);
        debug_puts("Received unexpected data bytes during handshake; resetting.");
        fakewire_exc_reset(fwe);
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
        assert(false);
    }
    mutex_unlock(&fwe->mutex);
}

static void fakewire_exc_on_recv_ctrl(void *opaque, fw_ctrl_t symbol) {
    fw_exchange_t *fwe = (fw_exchange_t *) opaque;
    assert(fwe != NULL);

#ifdef DEBUG
    debug_printf("Received control character: %s.", fakewire_codec_symbol(symbol));
#endif

    mutex_lock(&fwe->mutex);
    fakewire_exc_check_invariants(fwe);

    if (fwe->state == FW_EXC_DISCONNECTED) {
        // ignore control character
    } else if (fwe->recvid_state != FW_RID_IDLE && symbol != FWC_HANDSHAKE_1) {
        debug_printf("hit unexpected control character %s while waiting for handshake ID; resetting.",
                     fakewire_codec_symbol(symbol));
        fakewire_exc_reset(fwe);
    } else if (fwe->state == FW_EXC_HANDSHAKING) {
        switch (symbol) {
        case FWC_HANDSHAKE_1:
            // need to receive handshake ID next
            fwe->recvid_state = FW_RID_PRIMARY_ID;
            fwe->recvid_offset = 0;
            // abort sending a secondary handshake, in case we're already there
            fwe->needs_send_secondary_handshake = false;
            break;
        case FWC_HANDSHAKE_2:
            // need to receive handshake ID next
            fwe->recvid_state = FW_RID_SECONDARY_ID;
            fwe->recvid_offset = 0;
            break;
        case FWC_START_PACKET: // fallthrough
        case FWC_END_PACKET:   // fallthrough
        case FWC_ERROR_PACKET: // fallthrough
        case FWC_FLOW_CONTROL: // fallthrough
        case FWC_ESCAPE_SYM:
            debug_printf("hit unexpected control character %s during handshake; resetting.",
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
            debug_puts("Received handshake request during operating mode; resetting.");
            fakewire_exc_reset(fwe);
            fwe->recvid_state = FW_RID_PRIMARY_ID;
            fwe->recvid_offset = 0;
            break;
        case FWC_HANDSHAKE_2:
            if (fwe->has_last_sent_primary_id) {
                // late secondary handshake, likely because handshakes crossed in flight
                fwe->recvid_state = FW_RID_SECONDARY_ID;
                fwe->recvid_offset = 0;
            } else {
                // abort connection and restart everything
                debug_puts("Received unexpected secondary handshake during operating mode; resetting.");
                fakewire_exc_reset(fwe);
            }
            break;
        case FWC_START_PACKET:
            if (!fwe->has_sent_fct) {
                debug_puts("Received unauthorized start-of-packet; resetting.");
                fakewire_exc_reset(fwe);
            } else {
                assert(fwe->inbound_buffer != NULL); // should always have a buffer if we sent a FCT!
                assert(!fwe->inbound_read_done);     // if done hasn't been reset to false, we shouldn't have sent a FCT!
                assert(!fwe->recv_in_progress);

                fwe->has_sent_fct = false;
                fwe->recv_in_progress = true;
            }
            break;
        case FWC_END_PACKET:
            if (!fwe->recv_in_progress) {
                debug_puts("hit unexpected end-of-packet before start-of-packet; resetting.");
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
                debug_puts("hit unexpected error-end-of-packet before start-of-packet; resetting.");
                fakewire_exc_reset(fwe);
            } else {
                assert(fwe->inbound_buffer != NULL); // should always have a buffer if a read is in progress!
                assert(!fwe->inbound_read_done);
                // discard the data in the current packet
                fwe->inbound_buffer_offset = 0;
            }
            break;
        case FWC_FLOW_CONTROL:
            if (fwe->remote_sent_fct) {
                debug_puts("Received duplicate FCT; resetting.");
                fakewire_exc_reset(fwe);
            } else {
                fwe->remote_sent_fct = true;
                cond_broadcast(&fwe->cond);
            }
            break;
        case FWC_ESCAPE_SYM:
            // indicates that an invalid escape sequence was received by fakewire_codec
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

    ssize_t actual_len = -1;

    mutex_lock(&fwe->mutex);
    while (fwe->state != FW_EXC_DISCONNECTED) {
        fakewire_exc_check_invariants(fwe);

        // wait until handshake completes and receive is possible
        if (fwe->state == FW_EXC_HANDSHAKING || fwe->inbound_buffer != NULL) {
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
        assert(!fwe->has_sent_fct);
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

    return actual_len;
}

int fakewire_exc_write(fw_exchange_t *fwe, uint8_t *packet_in, size_t packet_len) {
    assert(fwe != NULL);

    mutex_lock(&fwe->mutex);
    // wait until handshake completes and transmit is possible
    while (fwe->state != FW_EXC_OPERATING || !fwe->remote_sent_fct || fwe->tx_busy) {
        fakewire_exc_check_invariants(fwe);

        if (fwe->state == FW_EXC_DISCONNECTED) {
            mutex_unlock(&fwe->mutex);
            return -1;
        }

        cond_wait(&fwe->cond, &fwe->mutex);
    }

    assert(fwe->tx_busy == false);
    assert(fwe->remote_sent_fct == true);
    fwe->tx_busy = true;
    fwe->remote_sent_fct = false;

    mutex_unlock(&fwe->mutex);

    // now actual transmit

    fw_receiver_t *link_write = fakewire_link_interface(&fwe->io_port);

    link_write->recv_ctrl(link_write->param, FWC_START_PACKET);
    link_write->recv_data(link_write->param, packet_in, packet_len);
    link_write->recv_ctrl(link_write->param, FWC_END_PACKET);

    // now let another packet have its turn
    mutex_lock(&fwe->mutex);
    assert(fwe->tx_busy == true);
    fwe->tx_busy = false;
    cond_broadcast(&fwe->cond);
    mutex_unlock(&fwe->mutex);

    return 0;
}

static uint64_t handshake_period(void) {
    uint64_t five_ms = 5 * 1000 * 1000;
    return (rand() % five_ms) + five_ms;
}

static void *fakewire_exc_flowtx_loop(void *fwe_opaque) {
    fw_exchange_t *fwe = (fw_exchange_t *) fwe_opaque;
    assert(fwe != NULL);

    uint64_t next_handshake = clock_timestamp_monotonic() + handshake_period();

    mutex_lock(&fwe->mutex);
    while (fwe->state != FW_EXC_DISCONNECTED) {
        fakewire_exc_check_invariants(fwe);

        uint64_t bound_ns = 0;

        if (fwe->state == FW_EXC_HANDSHAKING && !fwe->tx_busy) {
            // if we're handshaking... then we need to send primary handshakes on a regular basis
            uint64_t now = clock_timestamp_monotonic();

            uint32_t handshake_id;
            fw_ctrl_t handshake = FWC_NONE;

            if (fwe->needs_send_secondary_handshake) {
                handshake_id = fwe->primary_id;
                handshake = FWC_HANDSHAKE_2;
            } else if (now >= next_handshake) {
                // pick something very likely to be distinct (Go picks msb unset, C picks msb set)
                handshake_id = htonl(0x80000000 + (0x7FFFFFFF & (uint32_t) clock_timestamp_monotonic()));
                handshake = FWC_HANDSHAKE_1;
                fwe->last_sent_primary_id = handshake_id;
                fwe->has_last_sent_primary_id = true;
                fwe->sent_primary_handshake = true;
            }

            if (handshake != FWC_NONE) {
                fwe->tx_busy = true;
                mutex_unlock(&fwe->mutex);

                fw_receiver_t *link_write = fakewire_link_interface(&fwe->io_port);

                link_write->recv_ctrl(link_write->param, handshake);
                link_write->recv_data(link_write->param, (uint8_t*) &handshake_id, sizeof(handshake_id));

                mutex_lock(&fwe->mutex);
                assert(fwe->tx_busy == true);
                fwe->tx_busy = false;

                if (handshake == FWC_HANDSHAKE_2) {
                    if (!fwe->needs_send_secondary_handshake) {
                        debug_printf("Sent secondary handshake with ID=0x%08x, but request revoked by reset; not transitioning.", ntohl(handshake_id));
                    } else if (handshake_id != fwe->primary_id) {
                        // We HAVE to reset here. We can't just not transition. This is because we sent a secondary
                        // handshake that we KNOW the other end is going to reject. However, it might have already sent
                        // us a secondary handshake and flow control token of its own; if we don't reset our end of the
                        // connection, we might transmit a packet in response to that data, which the other end would
                        // promptly drop.
                        debug_printf("Sent secondary handshake with ID=0x%08x, but new primary ID=0x%08x had been received in the meantime; resetting.",
                                     ntohl(handshake_id), ntohl(fwe->primary_id));
                        fakewire_exc_reset(fwe);
                    } else {
                        debug_printf("Sent secondary handshake with ID=0x%08x; transitioning to operating mode.", ntohl(handshake_id));
                        fwe->state = FW_EXC_OPERATING;
                        fwe->needs_send_secondary_handshake = false;
                    }
                } else {
                    debug_printf("Sent primary handshake with ID=0x%08x.", ntohl(handshake_id));
                }

                cond_broadcast(&fwe->cond);

                now = clock_timestamp_monotonic();
                next_handshake = now + handshake_period();
            }

            if (now < next_handshake) {
                bound_ns = next_handshake - now;
            }
        }
        if (fwe->state == FW_EXC_OPERATING && fwe->inbound_buffer != NULL && !fwe->tx_busy &&
               !fwe->has_sent_fct && !fwe->recv_in_progress && !fwe->inbound_read_done) {
            // if we're ready to receive data, but haven't sent a FCT, send one
#ifdef DEBUG
            debug_printf("Sending flow control token on inbound_buffer=%p (fwe=%p)", fwe->inbound_buffer, fwe);
#endif
            fwe->tx_busy = true;
            fwe->has_sent_fct = true;
            mutex_unlock(&fwe->mutex);

            fw_receiver_t *link_write = fakewire_link_interface(&fwe->io_port);

            link_write->recv_ctrl(link_write->param, FWC_FLOW_CONTROL);

            mutex_lock(&fwe->mutex);
            assert(fwe->tx_busy == true);
            fwe->tx_busy = false;
            cond_broadcast(&fwe->cond);
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
