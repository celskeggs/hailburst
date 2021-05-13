#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "fakewire_exc.h"
#include "thread.h"

static void *fakewire_exc_reader_loop(void *fwe_opaque);
static void *fakewire_exc_writer_loop(void *fwe_opaque);

void fakewire_exc_init(fw_exchange_t *fwe, const char *label) {
    memset(fwe, 0, sizeof(fw_exchange_t));
    fwe->state = FW_EXC_DISCONNECTED;
    fwe->label = label;

    mutex_init(&fwe->mutex);
    cond_init(&fwe->cond);
}

void fakewire_exc_destroy(fw_exchange_t *fwe) {
    assert(fwe->state == FW_EXC_DISCONNECTED);

    mutex_destroy(&fwe->mutex);
    cond_destroy(&fwe->cond);

    memset(fwe, 0, sizeof(fw_exchange_t));
}

void fakewire_exc_attach(fw_exchange_t *fwe, const char *path, int flags) {
    assert(fwe->state == FW_EXC_DISCONNECTED);

    fakewire_link_attach(&fwe->io_port, path, flags);
    fwe->state = FW_EXC_STARTED;
    fwe->inbound_buffer = fwe->outbound_buffer = NULL;
    fwe->has_sent_fct = fwe->remote_sent_fct = false;

    thread_create(&fwe->reader_thread, fakewire_exc_reader_loop, fwe);
    thread_create(&fwe->writer_thread, fakewire_exc_writer_loop, fwe);
}

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

static void fakewire_exc_signal_error(fw_exchange_t *fwe) {
    if (fwe->state == FW_EXC_DISCONNECTED) {
        // ignore... we're shutting down!
    } else {
        fwe->state = FW_EXC_ERRORED;
    }
}

#if 0
#define debug_puts(str) (printf("[%s] %s", fwe->label, str))
#define debug_printf(fmt, ...) (printf("[%s] " fmt, fwe->label, __VA_ARGS__))
#else
#define debug_puts(str) (assert(true))
#define debug_printf(fmt, ...) (assert(true))
#endif

static void *fakewire_exc_reader_loop(void *fwe_opaque) {
    fw_exchange_t *fwe = (fw_exchange_t *) fwe_opaque;
    assert(fwe != NULL);

    mutex_lock(&fwe->mutex);

    while (fwe->state > FW_EXC_DISCONNECTED && fwe->state < FW_EXC_ERRORED) {
        mutex_unlock(&fwe->mutex);

        fw_char_t ch = fakewire_link_read(&fwe->io_port);

        if (ch == FW_CTRL_ESC) {
            ch = fakewire_link_read(&fwe->io_port);

            mutex_lock(&fwe->mutex);

            if (ch == FW_PARITYFAIL) {
                fprintf(stderr, "fakewire_exc: hit parity failure on link; erroring\n");
                fakewire_exc_signal_error(fwe);
                break;
            } else if (ch != FW_CTRL_FCT) {
                fprintf(stderr, "fakewire_exc: hit ESC followed by non-FCT char 0x%x; erroring\n", ch);
                fakewire_exc_signal_error(fwe);
                break;
            }
            // NULL... we can discard this, unless we're still at the "started" step, in which case we can advance
            if (fwe->state == FW_EXC_STARTED) {
                fwe->state = FW_EXC_CONNECTING;
                cond_broadcast(&fwe->cond);
            }
            continue;
        }

        mutex_lock(&fwe->mutex);

        if (ch == FW_PARITYFAIL) {
            fprintf(stderr, "fakewire_exc: hit parity failure on link; erroring\n");
            fakewire_exc_signal_error(fwe);
            break;
        } else if (ch == FW_CTRL_FCT) {
            if (fwe->remote_sent_fct) {
                fprintf(stderr, "fakewire_exc: hit duplicate FCT from remote; erroring\n");
                fakewire_exc_signal_error(fwe);
                break;
            }
            fwe->remote_sent_fct = true;
            debug_puts("reader: remote_sent_fct = true\n");
            if (fwe->state == FW_EXC_CONNECTING) {
                fwe->state = FW_EXC_RUN;
            }
            cond_broadcast(&fwe->cond);
        } else if (fwe->state == FW_EXC_CONNECTING) {
            fprintf(stderr, "fakewire_exc: hit unexpected character %x before first FCT was received; erroring\n", ch);
            fakewire_exc_signal_error(fwe);
            break;
        } else if (!fwe->has_sent_fct) {
            fprintf(stderr, "[%s] fakewire_exc: hit unexpected character %x before FCT was sent; erroring\n", fwe->label, ch);
            fakewire_exc_signal_error(fwe);
            break;
        } else {
            assert(fwe->inbound_buffer != NULL); // should always have a buffer if we sent a FCT!
            assert(fwe->state == FW_EXC_RUN);    // should always be RUN if we sent a FCT and aren't CONNECTING!
            assert(!fwe->inbound_read_done);     // if done hasn't been reset to false, we shouldn't have sent a FCT!

            if (ch == FW_CTRL_EEP) {
                // discard data in current packet
                fwe->inbound_buffer_offset = 0;
            } else if (ch == FW_CTRL_EOP) {
                fwe->inbound_read_done = true;
                fwe->has_sent_fct = false;
                debug_puts("reader: has_sent_fct = false\n");

                cond_broadcast(&fwe->cond);
            } else if (FW_IS_CTRL(ch)) {
                fprintf(stderr, "fakewire_exc: hit unexpected character %x instead of data character; erroring\n", ch);
                fakewire_exc_signal_error(fwe);
                break;
            } else {
                if (fwe->inbound_buffer_offset < fwe->inbound_buffer_max) {
                    fwe->inbound_buffer[fwe->inbound_buffer_offset] = FW_DATA(ch);
                }
                // keep incrementing even if we overflow so that the reader can tell that the packet was truncated
                fwe->inbound_buffer_offset += 1;
            }
        }
	}

    cond_broadcast(&fwe->cond);

    mutex_unlock(&fwe->mutex);

	return NULL;
}

static void *fakewire_exc_writer_loop(void *fwe_opaque) {
    fw_exchange_t *fwe = (fw_exchange_t *) fwe_opaque;
    assert(fwe != NULL);

    bool first_iter = true;

    mutex_lock(&fwe->mutex);

    while (fwe->state > FW_EXC_DISCONNECTED && fwe->state < FW_EXC_ERRORED) {
        if (!fakewire_link_write_ok(&fwe->io_port)) {
            fprintf(stderr, "fakewire_exc: encountered write failure; erroring\n");
            fakewire_exc_signal_error(fwe);
            break;
        }

        if (fwe->state == FW_EXC_STARTED || first_iter) {
            debug_puts("writer: case 1\n");
            mutex_unlock(&fwe->mutex);

            // send NULL (ESC+FCT)
            fakewire_link_write(&fwe->io_port, FW_CTRL_ESC);
            fakewire_link_write(&fwe->io_port, FW_CTRL_FCT);

            // TODO: should I stop sleeping early if the state changes?
            usleep(5000); // 5 ms spacing after NULL to avoid flooding the link

            mutex_lock(&fwe->mutex);
            first_iter = false;
            debug_puts("writer: finished case 1\n");
        } else if ((fwe->state == FW_EXC_CONNECTING || fwe->state == FW_EXC_RUN) && fwe->inbound_buffer != NULL && !fwe->has_sent_fct && !fwe->inbound_read_done) {
            debug_puts("writer: case 2\n");

            assert(!fwe->has_sent_fct);
            fwe->has_sent_fct = true;
            debug_puts("writer: has_sent_fct = true\n");
            cond_broadcast(&fwe->cond);

            mutex_unlock(&fwe->mutex);

            // send FCT to allow first/subsequent packet
            fakewire_link_write(&fwe->io_port, FW_CTRL_FCT);
            // send NULL to make sure it gets encoded into a byte
            fakewire_link_write(&fwe->io_port, FW_CTRL_ESC);
            fakewire_link_write(&fwe->io_port, FW_CTRL_FCT);

            mutex_lock(&fwe->mutex);

            debug_puts("writer: finished case 2\n");
        } else if (fwe->state == FW_EXC_RUN && fwe->outbound_buffer != NULL && fwe->remote_sent_fct && !fwe->outbound_write_done) {
            if (fwe->outbound_buffer_offset < fwe->outbound_buffer_max) {
                debug_puts("writer: case 3\n");
                fw_char_t nextch = FW_DATA(fwe->outbound_buffer[fwe->outbound_buffer_offset++]);

                mutex_unlock(&fwe->mutex);

                fakewire_link_write(&fwe->io_port, nextch);

                mutex_lock(&fwe->mutex);
                debug_puts("writer: finished case 3\n");
            } else {
                debug_puts("writer: case 4\n");
                fwe->remote_sent_fct = false;
                debug_puts("writer: remote_sent_fct = false\n");

                cond_broadcast(&fwe->cond);

                mutex_unlock(&fwe->mutex);

                fakewire_link_write(&fwe->io_port, FW_CTRL_EOP);
                // plus an extra NULL to force the last bits over the wire
                fakewire_link_write(&fwe->io_port, FW_CTRL_ESC);
                fakewire_link_write(&fwe->io_port, FW_CTRL_FCT);

                mutex_lock(&fwe->mutex);

                fwe->outbound_write_done = true;
                cond_broadcast(&fwe->cond);
                debug_puts("writer: finished case 4\n");
            }
        } else {
            debug_printf("writer: case 5 (%d, %p, %d, %d)\n", fwe->state, fwe->outbound_buffer, fwe->remote_sent_fct, fwe->outbound_write_done);
            cond_wait(&fwe->cond, &fwe->mutex);
            debug_puts("writer: finished case 5\n");
        }
    }

    mutex_unlock(&fwe->mutex);

    return NULL;
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

int fakewire_exc_write(fw_exchange_t *fwe, uint8_t *packet_in, size_t packet_len) {
    assert(fwe != NULL);

    mutex_lock(&fwe->mutex);
    while (fwe->outbound_buffer != NULL) {
        if (fwe->state == FW_EXC_DISCONNECTED || fwe->state == FW_EXC_ERRORED) {
            mutex_unlock(&fwe->mutex);
            return -1;
        }

        cond_wait(&fwe->cond, &fwe->mutex);
    }
    fwe->outbound_buffer = packet_in;
    fwe->outbound_buffer_offset = 0;
    fwe->outbound_buffer_max = packet_len;
    fwe->outbound_write_done = false;

    cond_broadcast(&fwe->cond);

    while (!fwe->outbound_write_done) {
        if (fwe->state == FW_EXC_DISCONNECTED || fwe->state == FW_EXC_ERRORED) {
            fwe->outbound_buffer = NULL;
            mutex_unlock(&fwe->mutex);
            return -1;
        }

        cond_wait(&fwe->cond, &fwe->mutex);
    }

    assert(fwe->outbound_buffer == packet_in);
    assert(fwe->outbound_buffer_offset == packet_len);
    assert(fwe->outbound_buffer_max == packet_len);
    fwe->outbound_buffer = NULL;

    cond_broadcast(&fwe->cond);

    mutex_unlock(&fwe->mutex);

    return 0;
}
