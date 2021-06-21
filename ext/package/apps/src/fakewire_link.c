#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "debug.h"
#include "fakewire_link.h"

enum {
    FW_LINK_RING_SIZE = 1024,
};

static void fakewire_link_recv_data(void *opaque, uint8_t *bytes_in, size_t bytes_count) {
    assert(opaque != NULL && bytes_in != NULL);
    assert(bytes_count > 0);
    fw_link_t *fwl = (fw_link_t*) opaque;

    fakewire_enc_encode_data(&fwl->encoder, bytes_in, bytes_count);
}

static void fakewire_link_recv_ctrl(void *opaque, fw_ctrl_t symbol) {
    assert(opaque != NULL);
    fw_link_t *fwl = (fw_link_t*) opaque;

    fakewire_enc_encode_ctrl(&fwl->encoder, symbol);
}

static void fakewire_link_parity_fail(void *opaque) {
    // do nothing. we'll just stop transmitting.
}

static void *fakewire_link_output_loop(void *opaque) {
    assert(opaque != NULL);
    fw_link_t *fwl = (fw_link_t*) opaque;

    char write_buf[FW_LINK_RING_SIZE];

    for (;;) {
        // read as many bytes as possible from ring buffer in one chunk
        size_t count_bytes = ringbuf_read(&fwl->enc_ring, write_buf, sizeof(write_buf), RB_BLOCKING);
        assert(count_bytes > 0 && count_bytes <= sizeof(write_buf));
        if (count_bytes < sizeof(write_buf)) {
            usleep(500); // wait half a millisecond to bunch related writes
            count_bytes += ringbuf_read(&fwl->enc_ring, write_buf + count_bytes, sizeof(write_buf) - count_bytes, RB_NONBLOCKING);
        }
        assert(count_bytes > 0 && count_bytes <= sizeof(write_buf));

        // write one large chunk to the output port
        ssize_t actual = write(fwl->fd_out, write_buf, count_bytes);
        if (actual != count_bytes) {
            debugf("fakewire_link_output_loop: write failed: %zd bytes instead of %zu bytes", actual, count_bytes);
            return NULL;
        }
    }
}

static void *fakewire_link_input_loop(void *opaque) {
    assert(opaque != NULL);
    fw_link_t *fwl = (fw_link_t*) opaque;

    uint8_t read_buf[1024];

    for (;;) {
        // read as many bytes as possible from the input port at once
        ssize_t actual = read(fwl->fd_in, read_buf, sizeof(read_buf));
        if (actual <= 0) { // 0 means EOF, <0 means error
            debugf("fakewire_link_read: attempt to read failed: %zd instead of %zu\n", actual, sizeof(read_buf));
            // read failure... end connection!
            fakewire_dec_eof(&fwl->decoder);
            return NULL;
        }
        assert(actual > 0 && actual <= sizeof(read_buf));

        // write as many bytes at once as possible
        fakewire_dec_decode(&fwl->decoder, read_buf, actual);
    }
}

void fakewire_link_init(fw_link_t *fwl, fw_receiver_t *receiver, const char *path, int flags) {
    assert(fwl != NULL && receiver != NULL && path != NULL);
    memset(fwl, 0, sizeof(fw_link_t));

    // first, configure all the data structures and interfaces
    fwl->interface = (fw_receiver_t) {
        .param = fwl,
        .recv_data = fakewire_link_recv_data,
        .recv_ctrl = fakewire_link_recv_ctrl,
        .parity_fail = fakewire_link_parity_fail,
    };
    ringbuf_init(&fwl->enc_ring, FW_LINK_RING_SIZE, 1);
    fakewire_enc_init(&fwl->encoder, &fwl->enc_ring);
    fakewire_dec_init(&fwl->decoder, receiver);

    // then let's open the file descriptors for our I/O backend of choice

    if (flags == FW_FLAG_FIFO_CONS || flags == FW_FLAG_FIFO_PROD) {
        // alternate mode for host testing via pipe

        char path_buf[strlen(path) + 10];
        snprintf(path_buf, sizeof(path_buf), "%s-c2p.pipe", path);
        int fd_c2p = open(path_buf, (flags == FW_FLAG_FIFO_CONS) ? O_WRONLY : O_RDONLY);
        snprintf(path_buf, sizeof(path_buf), "%s-p2c.pipe", path);
        int fd_p2c = open(path_buf, (flags == FW_FLAG_FIFO_PROD) ? O_WRONLY : O_RDONLY);

        if (fd_c2p < 0 || fd_p2c < 0) {
            perror("open");
            exit(1);
        }
        fwl->fd_in = (flags == FW_FLAG_FIFO_CONS) ? fd_p2c : fd_c2p;
        fwl->fd_out = (flags == FW_FLAG_FIFO_CONS) ? fd_c2p : fd_p2c;
    } else if (flags == FW_FLAG_VIRTIO) {
        fwl->fd_out = fwl->fd_in = open(path, O_RDWR | O_NOCTTY);
        if (fwl->fd_in < 0) {
            perror("open");
            exit(1);
        }
    } else {
        assert(flags == FW_FLAG_SERIAL);
        fwl->fd_out = fwl->fd_in = open(path, O_RDWR | O_NOCTTY | O_NDELAY);
        if (fwl->fd_in < 0) {
            perror("open");
            exit(1);
        }
        fcntl(fwl->fd_in, F_SETFL, 0);

        struct termios options;

        if (tcgetattr(fwl->fd_in, &options) < 0) {
            perror("tcgetattr");
            exit(1);
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
            exit(1);
        }
    }
    assert(fwl->fd_in != 0 && fwl->fd_out != 0);

    // and now let's set up the I/O threads
    thread_create(&fwl->output_thread, fakewire_link_output_loop, fwl);
    thread_create(&fwl->input_thread, fakewire_link_input_loop, fwl);
}

fw_receiver_t *fakewire_link_interface(fw_link_t *fwl) {
    return &fwl->interface;
}

/* for later, if we ever need to make fakewire_link_t objects destructible again
void fakewire_link_detach(fw_link_t *fwp) {
    assert(fwp->fd_in != 0 && fwp->fd_out != 0);
    printf("Detaching link...\n");
    if (fwp->fd_in >= 0 && fwp->fd_in != fwp->fd_out) {
        if (close(fwp->fd_in) < 0) {
            perror("close");
            exit(1);
        }
        fwp->fd_in = -1;
    }
    if (fwp->fd_out >= 0) {
        if (close(fwp->fd_out) < 0) {
            perror("close");
            exit(1);
        }
        fwp->fd_out = -1;
    }
    bit_buf_destroy(&fwp->readahead);
}
*/