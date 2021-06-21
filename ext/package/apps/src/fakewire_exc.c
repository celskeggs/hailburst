#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "debug.h"
#include "fakewire_exc.h"
#include "thread.h"

static void *fakewire_exc_flowtx_loop(void *fwe_opaque);

static void fakewire_exc_on_recv_data(void *opaque, uint8_t *bytes_in, size_t bytes_count);
static void fakewire_exc_on_recv_ctrl(void *opaque, fw_ctrl_t symbol);
static void fakewire_exc_on_parity_fail(void *opaque);

//#define DEBUG

void fakewire_exc_init(fw_exchange_t *fwe, const char *label) {
    memset(fwe, 0, sizeof(fw_exchange_t));
    fwe->state = FW_EXC_DISCONNECTED;
    fwe->label = label;

    fwe->link_interface = (fw_receiver_t) {
        .param = fwe,
        .recv_data = fakewire_exc_on_recv_data,
        .recv_ctrl = fakewire_exc_on_recv_ctrl,
        .parity_fail = fakewire_exc_on_parity_fail,
    };

    mutex_init(&fwe->mutex);
    cond_init(&fwe->cond);
    mutex_init(&fwe->tx_mutex);
}

/*
void fakewire_exc_destroy(fw_exchange_t *fwe) {
    assert(fwe->state == FW_EXC_DISCONNECTED);

    mutex_destroy(&fwe->mutex);
    cond_destroy(&fwe->cond);

    memset(fwe, 0, sizeof(fw_exchange_t));
}
*/

void fakewire_exc_attach(fw_exchange_t *fwe, const char *path, int flags) {
    assert(fwe->state == FW_EXC_DISCONNECTED);

    fakewire_link_init(&fwe->io_port, &fwe->link_interface, path, flags);
    fwe->state = FW_EXC_STARTED;
    fwe->inbound_buffer = NULL;
    fwe->has_sent_fct = fwe->remote_sent_fct = false;

    thread_create(&fwe->flowtx_thread, fakewire_exc_flowtx_loop, fwe);
}

/*
void fakewire_exc_detach(fw_exchange_t *fwe) {
    assert(fwe->state > FW_EXC_DISCONNECTED && fwe->state <= FW_EXC_ERRORED);

    mutex_lock(&fwe->mutex);

    fwe->state = FW_EXC_DISCONNECTED;
    cond_broadcast(&fwe->cond);

    mutex_unlock(&fwe->mutex);

    fakewire_link_detach(&fwe->io_port);

    thread_join(fwe->reader_thread);
    thread_join(fwe->writer_thread);
}
*/

static void fakewire_exc_signal_error(fw_exchange_t *fwe) {
    if (fwe->state == FW_EXC_DISCONNECTED) {
        // ignore... we're shutting down!
    } else {
        fwe->state = FW_EXC_ERRORED;
    }
}

#if 0
#define debug_puts(str) (debugf("[%s] %s", fwe->label, str))
#define debug_printf(fmt, ...) (debugf("[%s] " fmt, fwe->label, __VA_ARGS__))
#else
#define debug_puts(str) (assert(true))
#define debug_printf(fmt, ...) (assert(true))
#endif

static void fakewire_exc_on_recv_data(void *opaque, uint8_t *bytes_in, size_t bytes_count) {
    fw_exchange_t *fwe = (fw_exchange_t *) opaque;
    assert(fwe != NULL && bytes_in != NULL);
    assert(bytes_count > 0);

#ifdef DEBUG
    debugf("[fakewire_exc] Received %zu regular bytes.", bytes_count);
#endif

    mutex_lock(&fwe->mutex);

    assert(fwe->state >= FW_EXC_DISCONNECTED && fwe->state <= FW_EXC_ERRORED);

    if (fwe->state == FW_EXC_DISCONNECTED || fwe->state == FW_EXC_ERRORED) {
        // ignore data character
    } else if (fwe->recv_in_escape) {
        fprintf(stderr, "fakewire_exc: hit ESC followed by data char 0x%x; erroring\n", bytes_in[0]);
        fakewire_exc_signal_error(fwe);
    } else if (fwe->state == FW_EXC_CONNECTING) {
        fprintf(stderr, "fakewire_exc: hit unexpected data character 0x%x before first FCT was received; erroring\n", bytes_in[0]);
        fakewire_exc_signal_error(fwe);
    } else if (!fwe->has_sent_fct) {
        fprintf(stderr, "[%s] fakewire_exc: hit unexpected data character 0x%x before FCT was sent; erroring\n", fwe->label, bytes_in[0]);
        fakewire_exc_signal_error(fwe);
    } else {
        assert(fwe->inbound_buffer != NULL); // should always have a buffer if we sent a FCT!
        assert(fwe->state == FW_EXC_RUN);    // should always be RUN if we sent a FCT and aren't CONNECTING!
        assert(!fwe->inbound_read_done);     // if done hasn't been reset to false, we shouldn't have sent a FCT!

        size_t copy_n = fwe->inbound_buffer_max - fwe->inbound_buffer_offset;
        if (copy_n > bytes_count) {
            copy_n = bytes_count;
        }
        if (copy_n > 0) {
            memcpy(&fwe->inbound_buffer[fwe->inbound_buffer_offset], bytes_in, copy_n);
        }
        // keep incrementing even if we overflow so that the reader can tell that the packet was truncated
        fwe->inbound_buffer_offset += bytes_count;
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

    assert(fwe->state >= FW_EXC_DISCONNECTED && fwe->state <= FW_EXC_ERRORED);

    if (fwe->state == FW_EXC_DISCONNECTED || fwe->state == FW_EXC_ERRORED) {
        // ignore control character
    } else if (fwe->recv_in_escape) {
        if (symbol == FWC_FCT) {
            // NULL... we can discard this, unless we're still at the "started" step, in which case we can advance
            if (fwe->state == FW_EXC_STARTED) {
                fwe->state = FW_EXC_CONNECTING;
                cond_broadcast(&fwe->cond);
            }
            fwe->recv_in_escape = false;
        } else {
            fprintf(stderr, "fakewire_exc: hit ESC followed by non-FCT ctrl char 0x%x; erroring\n", symbol);
            fakewire_exc_signal_error(fwe);
        }
    } else if (symbol == FWC_ESC) {
        fwe->recv_in_escape = true;
    } else if (symbol == FWC_FCT) {
        if (fwe->remote_sent_fct) {
            fprintf(stderr, "fakewire_exc: hit duplicate FCT from remote; erroring\n");
            fakewire_exc_signal_error(fwe);
        } else {
            fwe->remote_sent_fct = true;
            debug_puts("reader: remote_sent_fct = true\n");
            if (fwe->state == FW_EXC_CONNECTING && fwe->has_sent_fct) {
                fwe->state = FW_EXC_RUN;
            }
            cond_broadcast(&fwe->cond);
        }
    } else if (fwe->state == FW_EXC_CONNECTING) {
        fprintf(stderr, "fakewire_exc: hit unexpected ctrl character %x before first FCT was received; erroring\n", symbol);
        fakewire_exc_signal_error(fwe);
    } else if (!fwe->has_sent_fct) {
        fprintf(stderr, "[%s] fakewire_exc: hit unexpected ctrl character %x before FCT was sent; erroring\n", fwe->label, symbol);
        fakewire_exc_signal_error(fwe);
    } else {
        assert(fwe->inbound_buffer != NULL); // should always have a buffer if we sent a FCT!
        assert(fwe->state == FW_EXC_RUN);    // should always be RUN if we sent a FCT and aren't CONNECTING!
        assert(!fwe->inbound_read_done);     // if done hasn't been reset to false, we shouldn't have sent a FCT!

        if (symbol == FWC_EEP) {
            // discard data in current packet
            fwe->inbound_buffer_offset = 0;
        } else {
            assert(symbol == FWC_EOP); // only remaining possibility

            fwe->inbound_read_done = true;
            fwe->has_sent_fct = false;
            debug_puts("reader: has_sent_fct = false\n");

            cond_broadcast(&fwe->cond);
        }
    }

    mutex_unlock(&fwe->mutex);
}

static void fakewire_exc_on_parity_fail(void *opaque) {
    fw_exchange_t *fwe = (fw_exchange_t *) opaque;
    assert(fwe != NULL);

#ifdef DEBUG
    debug0("[fakewire_exc] Detected parity failure.");
#endif

    mutex_lock(&fwe->mutex);

    if (fwe->state != FW_EXC_DISCONNECTED && fwe->state != FW_EXC_ERRORED) {
        assert(fwe->state > FW_EXC_DISCONNECTED && fwe->state < FW_EXC_ERRORED);

        fprintf(stderr, "fakewire_exc: hit parity failure on link; erroring\n");
        fakewire_exc_signal_error(fwe);
    }

    mutex_unlock(&fwe->mutex);
}

ssize_t fakewire_exc_read(fw_exchange_t *fwe, uint8_t *packet_out, size_t packet_max) {
    assert(fwe != NULL);

    mutex_lock(&fwe->mutex);
    while (fwe->inbound_buffer != NULL) {
        if (fwe->state == FW_EXC_DISCONNECTED || fwe->state == FW_EXC_ERRORED) {
            mutex_unlock(&fwe->mutex);
            return -1;
        }

        cond_wait(&fwe->cond, &fwe->mutex);
    }
    fwe->inbound_buffer = packet_out;
    fwe->inbound_buffer_offset = 0;
    fwe->inbound_buffer_max = packet_max;
    fwe->inbound_read_done = false;

    cond_broadcast(&fwe->cond);

    while (!fwe->inbound_read_done) {
        if (fwe->state == FW_EXC_DISCONNECTED || fwe->state == FW_EXC_ERRORED) {
            fwe->inbound_buffer = NULL;
            mutex_unlock(&fwe->mutex);
            return -1;
        }

        cond_wait(&fwe->cond, &fwe->mutex);
    }

    assert(fwe->inbound_buffer == packet_out);
    assert(fwe->inbound_buffer_max == packet_max);
    fwe->inbound_buffer = NULL;
    ssize_t offset = fwe->inbound_buffer_offset;
    assert(offset >= 0);

    cond_broadcast(&fwe->cond);

    mutex_unlock(&fwe->mutex);

    return offset;
}

static void *fakewire_exc_flowtx_loop(void *fwe_opaque) {
    fw_exchange_t *fwe = (fw_exchange_t *) fwe_opaque;
    assert(fwe != NULL);

    fw_receiver_t *link_write = fakewire_link_interface(&fwe->io_port);

    mutex_lock(&fwe->mutex);
    fw_exchange_state state = fwe->state;
    mutex_unlock(&fwe->mutex);

    if (state == FW_EXC_DISCONNECTED || state == FW_EXC_ERRORED) {
        return NULL;
    }

    assert(state > FW_EXC_DISCONNECTED && state < FW_EXC_ERRORED);

    // we send a series of NULLs when first establishing a connection

    for (;;) {
        // send NULL (ESC+FCT)
        mutex_lock(&fwe->tx_mutex);
        link_write->recv_ctrl(link_write->param, FWC_ESC);
        link_write->recv_ctrl(link_write->param, FWC_FCT);
        mutex_unlock(&fwe->tx_mutex);

        // grab state so we know if we should keep running
        mutex_lock(&fwe->mutex);
        state = fwe->state;
        mutex_unlock(&fwe->mutex);

        // we can stop sending NULLs once the other side confirms they've seen at least one
        if (state != FW_EXC_STARTED) {
            break;
        }

        // space them out a little
        usleep(5000);
    }

    // TODO: should we receive notification of write failures somehow, so that we can signal an error using fakewire_exc_signal_error and terminate the connection?

    // and then, we send FCTs whenever we're ready to receive data
    mutex_lock(&fwe->mutex);
    while (fwe->state == FW_EXC_CONNECTING || fwe->state == FW_EXC_RUN) {
        if (fwe->inbound_buffer != NULL && !fwe->has_sent_fct && !fwe->inbound_read_done) {
            // if we're ready to receive data, but haven't sent a FCT, send one

            mutex_lock(&fwe->tx_mutex);

            fwe->has_sent_fct = true;
            debug_puts("writer: has_sent_fct = true\n");
            cond_broadcast(&fwe->cond);

            mutex_unlock(&fwe->mutex);

            // send FCT to allow first/subsequent packet
            link_write->recv_ctrl(link_write->param, FWC_FCT);
            // send NULL to make sure the FCT gets encoded into a byte
            link_write->recv_ctrl(link_write->param, FWC_ESC);
            link_write->recv_ctrl(link_write->param, FWC_FCT);
            mutex_unlock(&fwe->tx_mutex);

            mutex_lock(&fwe->mutex);

            if (fwe->state == FW_EXC_CONNECTING && fwe->remote_sent_fct) {
                fwe->state = FW_EXC_RUN;
                cond_broadcast(&fwe->cond);
            }
        } else {
            // otherwise, wait until we do need to send an FCT

            cond_wait(&fwe->cond, &fwe->mutex);
        }
    }
    mutex_unlock(&fwe->mutex);

    return NULL;
}

int fakewire_exc_write(fw_exchange_t *fwe, uint8_t *packet_in, size_t packet_len) {
    assert(fwe != NULL);

    mutex_lock(&fwe->mutex);
    // wait until we are ready to send a packet
    while (fwe->state != FW_EXC_RUN || !fwe->remote_sent_fct) {
        if (fwe->state == FW_EXC_DISCONNECTED || fwe->state == FW_EXC_ERRORED) {
            mutex_unlock(&fwe->mutex);
            return -1;
        }

        assert(fwe->state > FW_EXC_DISCONNECTED && fwe->state < FW_EXC_ERRORED);
        cond_wait(&fwe->cond, &fwe->mutex);
    }

    // make sure only one packet is being attempted at a time; the others can wait their turn
    // (total order: acquire tx_mutex before mutex)
    mutex_lock(&fwe->tx_mutex);

    // clear the FCT marker
    assert(fwe->remote_sent_fct == true);
    fwe->remote_sent_fct = false;
    mutex_unlock(&fwe->mutex);

    // now actual transmit, with tx_mutex held for guaranteed ownership

    fw_receiver_t *link_write = fakewire_link_interface(&fwe->io_port);

    // transmit packet body
    link_write->recv_data(link_write->param, packet_in, packet_len);
    // transmit EOP marker
    link_write->recv_ctrl(link_write->param, FWC_EOP);
    // and an extra NULL to force the last bits over the wire
    link_write->recv_ctrl(link_write->param, FWC_ESC);
    link_write->recv_ctrl(link_write->param, FWC_FCT);

    // now let another packet have its turn
    mutex_unlock(&fwe->tx_mutex);

    return 0;
}
