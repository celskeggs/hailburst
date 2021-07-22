#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "debug.h"
#include "fakewire_link.h"

enum {
    FW_LINK_RING_SIZE = 1024,
};

//#define DEBUG

#define debug_puts(str) (debugf("[ fakewire_link] [%s] %s", fwl->label, str))
#define debug_printf(fmt, ...) (debugf("[ fakewire_link] [%s] " fmt, fwl->label, __VA_ARGS__))

static void fakewire_link_recv_data(void *opaque, uint8_t *bytes_in, size_t bytes_count) {
    assert(opaque != NULL && bytes_in != NULL);
    assert(bytes_count > 0);
    fw_link_t *fwl = (fw_link_t*) opaque;

#ifdef DEBUG
    debug_printf("Transmitting %zu regular bytes.", bytes_count);
#endif

    if (fakewire_enc_encode_data(&fwl->encoder, bytes_in, bytes_count) < 0) {
        // drop data; we're shutting down.
        assert(fwl->shutdown);
    }
}

static void fakewire_link_recv_ctrl(void *opaque, fw_ctrl_t symbol) {
    assert(opaque != NULL);
    fw_link_t *fwl = (fw_link_t*) opaque;

#ifdef DEBUG
    debug_printf("Transmitting control character: %s.", fakewire_codec_symbol(symbol));
#endif

    if (fakewire_enc_encode_ctrl(&fwl->encoder, symbol) < 0) {
        // drop data; we're shutting down.
        assert(fwl->shutdown);
    }
}

static void *fakewire_link_output_loop(void *opaque) {
    assert(opaque != NULL);
    fw_link_t *fwl = (fw_link_t*) opaque;

    char write_buf[FW_LINK_RING_SIZE];

    while (!fwl->shutdown) {
        // disable cancellation to simplify ring buffer code
        thread_disable_cancellation();

        // read as many bytes as possible from ring buffer in one chunk
        size_t count_bytes = ringbuf_read(&fwl->enc_ring, write_buf, sizeof(write_buf), RB_BLOCKING);
        if (count_bytes == 0 && fwl->shutdown) {
            break;
        }
        assert(count_bytes > 0 && count_bytes <= sizeof(write_buf));
#ifdef DEBUG
        debug_printf("Preliminary ringbuf_read produced %zu bytes.", count_bytes);
#endif
        if (count_bytes < sizeof(write_buf)) {
            usleep(500); // wait half a millisecond to bunch related writes
            count_bytes += ringbuf_read(&fwl->enc_ring, write_buf + count_bytes, sizeof(write_buf) - count_bytes, RB_NONBLOCKING);
#ifdef DEBUG
            debug_printf("Combined reads produced %zu bytes.", count_bytes);
#endif
        }
        assert(count_bytes > 0 && count_bytes <= sizeof(write_buf));

        // we don't check shutdown here, because we want to write any last bytes if we can... we'll wait for the
        // cancellation signal to arrive in 'write'.

        // enable cancellation to simplify termination
        thread_enable_cancellation();

        // write one large chunk to the output port
#ifdef DEBUG
        debug_printf("Writing %zu bytes to file descriptor...", count_bytes);
#endif
        ssize_t actual = write(fwl->fd_out, write_buf, count_bytes);
#ifdef DEBUG
        debug_printf("Finished writing %zd bytes to file descriptor.", actual);
#endif
        if (actual != count_bytes) {
            debug_printf("Write failed: %zd bytes instead of %zu bytes", actual, count_bytes);
            return NULL;
        }
    }
    return NULL;
}

static void *fakewire_link_input_loop(void *opaque) {
    assert(opaque != NULL);
    fw_link_t *fwl = (fw_link_t*) opaque;

    uint8_t read_buf[1024];

    while (!fwl->shutdown) {
        // enable cancellation to simplify termination
        thread_enable_cancellation();

        // read as many bytes as possible from the input port at once
        ssize_t actual = read(fwl->fd_in, read_buf, sizeof(read_buf));

        // disable cancellation so that the callback code we run doesn't have to be aware of cancellation
        thread_disable_cancellation();

        if (actual == 0 && fwl->shutdown) {
            return NULL;
        }
        if (actual <= 0) { // 0 means EOF, <0 means error
            debug_printf("Read failed: %zd when maximum was %zu", actual, sizeof(read_buf));
            return NULL;
        }
#ifdef DEBUG
        debug_printf("Read %zd bytes from file descriptor.", actual);
#endif
        assert(actual > 0 && actual <= sizeof(read_buf));

        if (fwl->shutdown) {
            break;
        }

        // write as many bytes at once as possible
        fakewire_dec_decode(&fwl->decoder, read_buf, actual);
    }
    return NULL;
}

int fakewire_link_init(fw_link_t *fwl, fw_receiver_t *receiver, const char *path, int flags, const char *label) {
    assert(fwl != NULL && receiver != NULL && path != NULL);
    memset(fwl, 0, sizeof(fw_link_t));

    // set up debug info real quick
    fwl->label = label;

    // first, let's open the file descriptors for our I/O backend of choice

    if (flags == FW_FLAG_FIFO_CONS || flags == FW_FLAG_FIFO_PROD) {
        // alternate mode for host testing via pipe

        if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
            perror("signal(SIGPIPE, SIG_IGN)");
            return -1;
        }

        char path_buf[strlen(path) + 10];
        snprintf(path_buf, sizeof(path_buf), "%s-c2p.pipe", path);
        int fd_c2p = open(path_buf, (flags == FW_FLAG_FIFO_CONS) ? O_WRONLY : O_RDONLY);
        snprintf(path_buf, sizeof(path_buf), "%s-p2c.pipe", path);
        int fd_p2c = open(path_buf, (flags == FW_FLAG_FIFO_PROD) ? O_WRONLY : O_RDONLY);

        if (fd_c2p < 0 || fd_p2c < 0) {
            perror("open");
            return -1;
        }
        fwl->fd_in = (flags == FW_FLAG_FIFO_CONS) ? fd_p2c : fd_c2p;
        fwl->fd_out = (flags == FW_FLAG_FIFO_CONS) ? fd_c2p : fd_p2c;
    } else if (flags == FW_FLAG_VIRTIO) {
        fwl->fd_out = fwl->fd_in = open(path, O_RDWR | O_NOCTTY);
        if (fwl->fd_in < 0) {
            perror("open");
            return -1;
        }
    } else {
        assert(flags == FW_FLAG_SERIAL);
        fwl->fd_out = fwl->fd_in = open(path, O_RDWR | O_NOCTTY | O_NDELAY);
        if (fwl->fd_in < 0) {
            perror("open");
            return -1;
        }
        fcntl(fwl->fd_in, F_SETFL, 0);

        struct termios options;

        if (tcgetattr(fwl->fd_in, &options) < 0) {
            perror("tcgetattr");
            return -1;
        }

        cfsetispeed(&options, B9600);
        cfsetospeed(&options, B9600);

        // don't attach
        options.c_cflag |= CLOCAL | CREAD;

        // 8-bit data
        options.c_cflag &= ~CSIZE;
        options.c_cflag |= CS8;

        // raw input
        options.c_iflag &= ~(IXON | IXOFF | ICRNL | IGNCR | INLCR | ISTRIP);
        options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG | IEXTEN);

        // raw output
        options.c_oflag &= ~OPOST;

        if (tcsetattr(fwl->fd_in, TCSANOW, &options) < 0) {
            perror("tcsetattr");
            return -1;
        }
    }
    assert(fwl->fd_in != 0 && fwl->fd_out != 0);

    // next, let's configure all the data structures and interfaces
    fwl->interface = (fw_receiver_t) {
        .param = fwl,
        .recv_data = fakewire_link_recv_data,
        .recv_ctrl = fakewire_link_recv_ctrl,
    };
    ringbuf_init(&fwl->enc_ring, FW_LINK_RING_SIZE, 1);
    fakewire_enc_init(&fwl->encoder, &fwl->enc_ring);
    fakewire_dec_init(&fwl->decoder, receiver);

    // and now let's set up the I/O threads
    fwl->shutdown = false;
    thread_create(&fwl->output_thread, fakewire_link_output_loop, fwl);
    thread_create(&fwl->input_thread, fakewire_link_input_loop, fwl);

    return 0;
}

fw_receiver_t *fakewire_link_interface(fw_link_t *fwl) {
    assert(fwl != NULL);

    return &fwl->interface;
}

void fakewire_link_shutdown(fw_link_t *fwl) {
    if (!fwl->shutdown) {
#ifdef DEBUG
        debug_puts("Shutting down link gracefully...");
#endif

        // first, attempt to shut down threads gracefully
        fwl->shutdown = true;

        // now shut down the ring buffer so that all further communication ceases... once the data currently in the
        // buffer has drained.
        ringbuf_shutdown(&fwl->enc_ring);
    }
}

void fakewire_link_destroy(fw_link_t *fwl) {
    assert(fwl != NULL);

    fakewire_link_shutdown(fwl);

    struct timespec ts;
    thread_time_now(&ts);
    // wait up to two seconds
    ts.tv_sec += 2;

#ifdef DEBUG
    debug_puts("Trying to join input thread nicely...");
#endif

    // make sure both threads end... forcefully, if necessary
    if (!thread_join_timed(fwl->input_thread, &ts)) {
#ifdef DEBUG
        debug_puts("Could not join input thread nicely; cancelling.");
#endif
        thread_cancel(fwl->input_thread);
#ifdef DEBUG
        debug_puts("Joining cancelled input thread.");
#endif
        thread_join(fwl->input_thread);
    }

#ifdef DEBUG
    debug_puts("Trying to join output thread nicely...");
#endif

    if (!thread_join_timed(fwl->output_thread, &ts)) {
#ifdef DEBUG
        debug_puts("Could not join output thread nicely; cancelling.");
#endif
        thread_cancel(fwl->output_thread);
#ifdef DEBUG
        debug_puts("Joining cancelled output thread.");
#endif
        thread_join(fwl->output_thread);
    }

#ifdef DEBUG
    debug_puts("All threads joined.");
#endif

    // close file descriptors
    assert(fwl->fd_in != 0 && fwl->fd_out != 0);
    if (close(fwl->fd_in) != 0) {
        perror("close");
    }
    if (fwl->fd_out != fwl->fd_in) {
        if (close(fwl->fd_out) != 0) {
            perror("close");
        }
    }
    fwl->fd_in = fwl->fd_out = -1;

    // no code needed here to destroy codecs... they're just structs

    // tear down ring buffer
    ringbuf_destroy(&fwl->enc_ring);

    // wipe memory to help mitigate use-after-destroy bugs
    memset(fwl, 0, sizeof(fw_link_t));
}
