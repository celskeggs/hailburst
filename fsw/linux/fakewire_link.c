#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <hal/debug.h>
#include <hal/timer.h>
#include <bus/link.h>

//#define DEBUG

enum {
    REPLICA_ID = 0,
};

#define debug_printf(lvl,fmt, ...) (debugf(lvl, "[%s] " fmt, fwl->options.label, ## __VA_ARGS__))

void fakewire_link_rx_clip(fw_link_t *fwl) {
    assert(fwl != NULL);

    assert(duct_message_size(fwl->rx_duct) == fwl->buffer_size);

    duct_txn_t txn;
    duct_send_prepare(&txn, fwl->rx_duct, REPLICA_ID);

    while (fwl->fd_in != -1 && duct_send_allowed(&txn)) {
        size_t received = 0;
        while (received < fwl->buffer_size) {
            // read as many bytes as possible from the input port at once
            ssize_t actual = read(fwl->fd_in, fwl->rx_buffer + received, fwl->buffer_size - received);
            if (actual == -1 && errno == EAGAIN) {
                break;
            }

            if (actual <= 0) { // 0 means EOF, <0 means error
                abortf("Read failed: %zd when maximum was %zu", actual, fwl->buffer_size);
                return;
            }
            received += actual;
        }

        if (received == 0) {
            break;
        }
#ifdef LINK_DEBUG
        debug_printf(TRACE, "Read %zd bytes from file descriptor.", received);
#endif

        duct_send_message(&txn, fwl->rx_buffer, received, timer_epoch_ns());
    }

    duct_send_commit(&txn);
}

void fakewire_link_tx_clip(fw_link_t *fwl) {
    assert(fwl != NULL);

    assert(duct_message_size(fwl->tx_duct) == fwl->buffer_size);

    duct_txn_t txn;
    duct_receive_prepare(&txn, fwl->tx_duct, REPLICA_ID);

    size_t total_blocked = 0;
    size_t total_written = 0;
    size_t size;
    while (fwl->fd_out != -1 && (size = duct_receive_message(&txn, fwl->tx_buffer, NULL)) > 0) {
        // write as many bytes as possible to the output port at once
        assert(size > 0 && size <= fwl->buffer_size);

        size_t remaining = size;
        uint8_t *data = fwl->tx_buffer;
        while (remaining > 0) {
            ssize_t actual = write(fwl->fd_out, data, remaining);
            if (actual == -1 && errno == EAGAIN) {
                total_blocked += remaining;
                goto blocked;
            }
            if (actual <= 0) {
                total_blocked += remaining;
                debug_printf(CRITICAL, "Write failed: %zd", actual);
                goto blocked;
            }
            assert(0 < actual && actual <= (ssize_t) remaining);
            remaining -= actual;
            data += actual;
            total_written += actual;
        }
    }
blocked:
    while ((size = duct_receive_message(&txn, NULL, NULL)) > 0) {
        total_blocked += size;
    }

#ifdef LINK_DEBUG
    if (total_written > 0) {
        debug_printf(TRACE, "Wrote %zu bytes to file descriptor.", total_written);
    }
#endif

    if (total_blocked > 0) {
        debug_printf(WARNING, "Failed to write %zu bytes to file descriptor.", total_blocked);
    }

    duct_receive_commit(&txn);
}

void fakewire_link_configure(fw_link_t *fwl) {
    assert(fwl != NULL);
    fw_link_options_t opts = fwl->options;

    int fd_in = -1, fd_out = -1;

    task_become_independent();

    // let's open the file descriptors for our I/O backend of choice
    // we have to do this in a separate thread, because it can block in the case of pipe connections
    if (opts.flags == FW_FLAG_FIFO_CONS || opts.flags == FW_FLAG_FIFO_PROD) {
        // alternate mode for host testing via pipe

        if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
            perror("signal(SIGPIPE, SIG_IGN)");
            abortf("Failed to ignore SIGPIPE signals, which is needed for pipe-based fakewire links.");
        }

        char path_buf[strlen(opts.path) + 10];
        snprintf(path_buf, sizeof(path_buf), "%s-c2p.pipe", opts.path);
        int fd_c2p = open(path_buf, (opts.flags == FW_FLAG_FIFO_CONS) ? O_WRONLY : O_RDONLY);
        snprintf(path_buf, sizeof(path_buf), "%s-p2c.pipe", opts.path);
        int fd_p2c = open(path_buf, (opts.flags == FW_FLAG_FIFO_PROD) ? O_WRONLY : O_RDONLY);

        if (fd_c2p < 0 || fd_p2c < 0) {
            perror("open");
            abortf("Failed to open pipes under '%s' for fakewire link.", opts.path);
        }
        fd_in = (opts.flags == FW_FLAG_FIFO_CONS) ? fd_p2c : fd_c2p;
        fd_out = (opts.flags == FW_FLAG_FIFO_CONS) ? fd_c2p : fd_p2c;
        fcntl(fd_in, F_SETFL, O_NONBLOCK);
        fcntl(fd_out, F_SETFL, O_NONBLOCK);
    } else if (opts.flags == FW_FLAG_VIRTIO) {
        fd_out = fd_in = open(opts.path, O_RDWR | O_NOCTTY);
        if (fd_in < 0) {
            perror("open");
            abortf("Failed to open VIRTIO serial port '%s' for fakewire link.", opts.path);
        }
        fcntl(fd_in, F_SETFL, O_NONBLOCK);
    } else {
        assert(opts.flags == FW_FLAG_SERIAL);
        fd_out = fd_in = open(opts.path, O_RDWR | O_NOCTTY | O_NDELAY);
        if (fd_in < 0) {
            perror("open");
            abortf("Failed to open serial port '%s' for fakewire link.", opts.path);
        }
        fcntl(fd_in, F_SETFL, O_NONBLOCK);

        struct termios options;

        if (tcgetattr(fd_in, &options) < 0) {
            perror("tcgetattr");
            abortf("Failed to retrieve serial port attributes from '%s' for fakewire link.", opts.path);
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

        if (tcsetattr(fd_in, TCSANOW, &options) < 0) {
            perror("tcsetattr");
            abortf("Failed to set serial port attributes from '%s' for fakewire link.", opts.path);
        }
    }
    assert(fd_in > 0 && fd_out > 0);

    task_become_dependent();

    atomic_store(fwl->fd_in, fd_in);
    atomic_store(fwl->fd_out, fd_out);
}
