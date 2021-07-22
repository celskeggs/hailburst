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

#if 0
#define debug_puts(str) (debugf("[%s] %s", fwe->label, str))
#define debug_printf(fmt, ...) (debugf("[%s] " fmt, fwe->label, __VA_ARGS__))
#else
#define debug_puts(str) (assert(true))
#define debug_printf(fmt, ...) (assert(true))
#endif

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
    fwe->primary_id = fwe->secondary_id = fwe->sent_primary_id = 0;

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

    if (fakewire_link_init(&fwe->io_port, &fwe->link_interface, path, flags) < 0) {
        mutex_unlock(&fwe->mutex);
        return -1;
    }
    fakewire_exc_reset(fwe);

    thread_create(&fwe->flowtx_thread, fakewire_exc_flowtx_loop, fwe);
    mutex_unlock(&fwe->mutex);
    return 0;
}

void fakewire_exc_detach(fw_exchange_t *fwe) {
    // acquire lock and check assumptions
    mutex_lock(&fwe->mutex);
    fakewire_exc_check_invariants(fwe);

    assert(fwe->state != FW_EXC_DISCONNECTED);
    assert(!fwe->detaching);
    pthread_t flowtx_thread = fwe->flowtx_thread;

    // set state to cause teardown
    fwe->state = FW_EXC_DISCONNECTED;
    fwe->detaching = true;
    cond_broadcast(&fwe->cond);

    // wait until flowtx thread terminates
    mutex_unlock(&fwe->mutex);
    thread_join(flowtx_thread);
    mutex_lock(&fwe->mutex);

    // clear flowtx thread handle
    assert(fwe->flowtx_thread == flowtx_thread);

    // wait until all transmissions complete
    while (fwe->tx_busy) {
        cond_wait(&fwe->cond, &fwe->mutex);
    }

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
    debugf("[fakewire_exc] Received %zu regular bytes.", bytes_count);
#endif

    mutex_lock(&fwe->mutex);
    fakewire_exc_check_invariants(fwe);

    if (fwe->state == FW_EXC_DISCONNECTED) {
        // ignore data character
        return;
    } else if (fwe->state == FW_EXC_HANDSHAKING) {
        if (fwe->inbound_buffer == NULL) {
            debug0("[fakewire_exc] Received unexpected data bytes during handshake; resetting.\n");
            fakewire_exc_reset(fwe);
            return;
        }
        assert(fwe->inbound_buffer == (uint8_t*) &fwe->primary_id || fwe->inbound_buffer == (uint8_t*) &fwe->secondary_id);
        assert(fwe->inbound_buffer_max == sizeof(uint32_t));
    } else if (fwe->state == FW_EXC_OPERATING) {
        if (!fwe->has_sent_fct) {
            fprintf(stderr, "[%s] fakewire_exc: hit unexpected data character 0x%x before FCT was sent; resetting.\n", fwe->label, bytes_in[0]);
            fakewire_exc_reset(fwe);
            return;
        }
        assert(fwe->inbound_buffer != NULL);
        if (!fwe->recv_in_progress) {
            fprintf(stderr, "[%s] fakewire_exc: hit unexpected data character 0x%x before start-of-packet; resetting.\n", fwe->label, bytes_in[0]);
            fakewire_exc_reset(fwe);
            return;
        }
    } else {
        assert(false);
    }
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

    if (fwe->state == FW_EXC_HANDSHAKING) {
        if (fwe->inbound_buffer_offset > sizeof(uint32_t)) {
            fprintf(stderr, "[%s] fakewire_exc: received too many data characters during handshake; resetting.\n", fwe->label);
            fakewire_exc_reset(fwe);
            return;
        }
        if (fwe->inbound_buffer == (uint8_t*) &fwe->primary_id) {
            fwe->inbound_buffer = NULL;
            debugf("[fakewire_exc] Received primary handshake with ID=0x%08x\n", ntohl(fwe->primary_id));

            // have the flowtx thread transmit a secondary handshake
            fwe->needs_send_secondary_handshake = true;
            cond_broadcast(&fwe->cond);
        } else if (fwe->inbound_buffer == (uint8_t*) &fwe->secondary_id) {
            fwe->inbound_buffer = NULL;
            if (!fwe->sent_primary_handshake) {
                fprintf(stderr, "[%s] fakewire_exc: received secondary handshake with ID=0x%08x when primary had not been sent; resetting.\n",
                        fwe->label, ntohl(fwe->secondary_id));
                fakewire_exc_reset(fwe);
                return;
            }
            if (fwe->sent_primary_id != fwe->secondary_id) {
                fprintf(stderr, "[%s] fakewire_exc: received mismatched secondary ID 0x%08x instead of 0x%08x; resetting.\n",
                        fwe->label, ntohl(fwe->secondary_id), ntohl(fwe->sent_primary_id));
                fakewire_exc_reset(fwe);
                return;
            }
            debugf("[fakewire_exc] Received secondary handshake with ID=0x%08x; transitioning to operating mode.\n", ntohl(fwe->secondary_id));
            fwe->state = FW_EXC_OPERATING;
            cond_broadcast(&fwe->cond);
        } else {
            assert(false);
        }
    }

    mutex_unlock(&fwe->mutex);
}

static void fakewire_exc_on_recv_ctrl(void *opaque, fw_ctrl_t symbol) {
    fw_exchange_t *fwe = (fw_exchange_t *) opaque;
    assert(fwe != NULL);

#ifdef DEBUG
    debugf("[fakewire_exc] Received control character: %d.", symbol);
#endif

    mutex_lock(&fwe->mutex);
    fakewire_exc_check_invariants(fwe);

    if (fwe->state == FW_EXC_DISCONNECTED) {
        // ignore control character
    } else if (fwe->state == FW_EXC_HANDSHAKING) {
        switch (symbol) {
        case FWC_HANDSHAKE_1:
            // need to receive handshake ID next
            assert(fwe->inbound_buffer == NULL || fwe->inbound_buffer == (uint8_t*) &fwe->secondary_id || fwe->inbound_buffer == (uint8_t*) &fwe->primary_id);
            fwe->inbound_buffer = (uint8_t*) &fwe->primary_id;
            fwe->inbound_buffer_offset = 0;
            fwe->inbound_buffer_max = sizeof(fwe->primary_id);
            fwe->inbound_read_done = false;
            break;
        case FWC_HANDSHAKE_2:
            // need to receive handshake ID next
            if (fwe->inbound_buffer == (uint8_t*) &fwe->primary_id || fwe->inbound_buffer == (uint8_t*) &fwe->secondary_id) {
                fprintf(stderr, "[%s] fakewire_exc: hit unexpected secondary handshake while waiting for handshake ID; resetting.\n", fwe->label);
                fakewire_exc_reset(fwe);
                break;
            }
            assert(fwe->inbound_buffer == NULL);
            fwe->inbound_buffer = (uint8_t*) &fwe->secondary_id;
            fwe->inbound_buffer_offset = 0;
            fwe->inbound_buffer_max = sizeof(fwe->secondary_id);
            fwe->inbound_read_done = false;
            break;
        case FWC_START_PACKET: // fallthrough
        case FWC_END_PACKET:   // fallthrough
        case FWC_ERROR_PACKET: // fallthrough
        case FWC_FLOW_CONTROL: // fallthrough
        case FWC_ESCAPE_SYM:
            fprintf(stderr, "[%s] fakewire_exc: hit unexpected control character 0x%x during handshake; resetting.\n", fwe->label, symbol);
            fakewire_exc_reset(fwe);
            break;
        default:
            assert(false);
        }
    } else if (fwe->state == FW_EXC_OPERATING) {
        switch (symbol) {
        case FWC_HANDSHAKE_1:
            // abort connection and restart everything
            fprintf(stderr, "[%s] fakewire_exc: received handshake request during operating mode; resetting.\n", fwe->label);
            fakewire_exc_reset(fwe);
            fwe->inbound_buffer = (uint8_t*) &fwe->primary_id;
            break;
        case FWC_HANDSHAKE_2:
            // abort connection and restart everything
            fprintf(stderr, "[%s] fakewire_exc: received unexpected secondary handshake during operating mode; resetting.\n", fwe->label);
            fakewire_exc_reset(fwe);
            break;
        case FWC_START_PACKET:
            if (!fwe->has_sent_fct) {
                fprintf(stderr, "[%s] fakewire_exc: received unauthorized start-of-packet; resetting.\n", fwe->label);
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
                fprintf(stderr, "[%s] fakewire_exc: hit unexpected end-of-packet before start-of-packet; resetting.\n", fwe->label);
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
                fprintf(stderr, "[%s] fakewire_exc: hit unexpected end-of-packet before start-of-packet; resetting.\n", fwe->label);
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
                fprintf(stderr, "[%s] fakewire_exc: received duplicate FCT; resetting.\n", fwe->label);
                fakewire_exc_reset(fwe);
            } else {
                debug_puts("reader: remote_sent_fct = true\n");
                fwe->remote_sent_fct = true;
                cond_broadcast(&fwe->cond);
            }
            break;
        case FWC_ESCAPE_SYM:
            // indicates that an invalid escape sequence was received by fakewire_codec
            fprintf(stderr, "[%s] fakewire_exc: received invalid escape sequence; resetting.\n", fwe->label);
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

static void *fakewire_exc_flowtx_loop(void *fwe_opaque) {
    fw_exchange_t *fwe = (fw_exchange_t *) fwe_opaque;
    assert(fwe != NULL);

    uint64_t handshake_period = 2 * 1000 * 1000; // every 2 milliseconds
    uint64_t next_handshake = clock_timestamp_monotonic() + handshake_period;

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
                // pick something very likely to be distinct
                handshake_id = htonl(0x80000000 + (0x7FFFFFFF & (uint32_t) clock_timestamp_monotonic()));
                handshake = FWC_HANDSHAKE_1;
                fwe->sent_primary_id = handshake_id;
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
                    debugf("[fakewire_exc] Sent secondary handshake with ID=0x%08x; transitioning to operating mode.\n", handshake_id);
                    fwe->state = FW_EXC_OPERATING;
                } else {
                    debugf("[fakewire_exc] Sent primary handshake with ID=0x%08x.\n", handshake_id);
                }

                cond_broadcast(&fwe->cond);

                now = clock_timestamp_monotonic();
                next_handshake = now + handshake_period;
            }

            if (now < next_handshake) {
                bound_ns = next_handshake - now;
            }
        }
        if (fwe->state == FW_EXC_OPERATING &&
               fwe->inbound_buffer != NULL && !fwe->has_sent_fct && !fwe->recv_in_progress && !fwe->inbound_read_done &&
               !fwe->tx_busy) {
            // if we're ready to receive data, but haven't sent a FCT, send one
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
