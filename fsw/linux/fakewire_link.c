#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <fsw/clock.h>
#include <fsw/debug.h>
#include <fsw/fakewire/link.h>

//#define DEBUG

#define debug_printf(lvl,fmt, ...) (debugf(lvl, "[%s] " fmt, fwl->label, ## __VA_ARGS__))

static void fakewire_link_rx_loop(void *opaque) {
    assert(opaque != NULL);
    fw_link_t *fwl = (fw_link_t*) opaque;

    while (true) {
        struct io_rx_ent *entry = chart_request_start(fwl->rx_chart);
        if (entry == NULL) {
            // wait for another entry to be available
            semaphore_take(&fwl->receive_wake);
            continue;
        }
        // read as many bytes as possible from the input port at once
        size_t max = io_rx_size(fwl->rx_chart);
        ssize_t actual = read(fwl->fd_in, entry->data, max);

        if (actual <= 0) { // 0 means EOF, <0 means error
            debug_printf(CRITICAL, "Read failed: %zd when maximum was %zu", actual, max);
            break;
        }
#ifdef DEBUG
        debug_printf(TRACE, "Read %zd bytes from file descriptor.", actual);
#endif
        assert(actual > 0 && actual <= (ssize_t) max);

        entry->receive_timestamp = clock_timestamp();
        entry->actual_length = (uint32_t) actual;

        chart_request_send(fwl->rx_chart, 1);
    }
}

static void fakewire_link_tx_loop(void *opaque) {
    assert(opaque != NULL);
    fw_link_t *fwl = (fw_link_t*) opaque;

    while (true) {
        struct io_tx_ent *entry = chart_reply_start(fwl->tx_chart);
        if (entry == NULL) {
            // wait for another entry to be available
            semaphore_take(&fwl->transmit_wake);
            continue;
        }
        // read as many bytes as possible from the input port at once
        assert(entry->actual_length > 0 && entry->actual_length <= io_tx_size(fwl->tx_chart));

        ssize_t actual = write(fwl->fd_out, entry->data, entry->actual_length);

        if (actual == (ssize_t) entry->actual_length) {
#ifdef DEBUG
            debug_printf(TRACE, "Finished writing %zd bytes to file descriptor.", actual);
#endif
        } else {
            debug_printf(CRITICAL, "Write failed: %zd bytes instead of %zu bytes", actual, entry->actual_length);
        }

        chart_reply_send(fwl->tx_chart, 1);
    }
}

static void fakewire_link_notify_rx_chart(void *opaque) {
    fw_link_t *fwl = (fw_link_t *) opaque;
    assert(fwl != NULL);

    // we don't worry about wakeups getting dropped.
    // that just means a previous wakeup is still pending, which is just as good!
    (void) semaphore_give(&fwl->receive_wake);
}

static void fakewire_link_notify_tx_chart(void *opaque) {
    fw_link_t *fwl = (fw_link_t *) opaque;
    assert(fwl != NULL);

    // we don't worry about wakeups getting dropped.
    // that just means a previous wakeup is still pending, which is just as good!
    (void) semaphore_give(&fwl->transmit_wake);
}

int fakewire_link_init(fw_link_t *fwl, fw_link_options_t opts, chart_t *data_rx, chart_t *data_tx) {
    assert(fwl != NULL && data_rx != NULL && opts.label != NULL && opts.path != NULL);
    memset(fwl, 0, sizeof(fw_link_t));

    // set up debug info real quick
    fwl->label = opts.label;

    // first, let's open the file descriptors for our I/O backend of choice

    if (opts.flags == FW_FLAG_FIFO_CONS || opts.flags == FW_FLAG_FIFO_PROD) {
        // alternate mode for host testing via pipe

        if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
            perror("signal(SIGPIPE, SIG_IGN)");
            return -1;
        }

        char path_buf[strlen(opts.path) + 10];
        snprintf(path_buf, sizeof(path_buf), "%s-c2p.pipe", opts.path);
        int fd_c2p = open(path_buf, (opts.flags == FW_FLAG_FIFO_CONS) ? O_WRONLY : O_RDONLY);
        snprintf(path_buf, sizeof(path_buf), "%s-p2c.pipe", opts.path);
        int fd_p2c = open(path_buf, (opts.flags == FW_FLAG_FIFO_PROD) ? O_WRONLY : O_RDONLY);

        if (fd_c2p < 0 || fd_p2c < 0) {
            perror("open");
            return -1;
        }
        fwl->fd_in = (opts.flags == FW_FLAG_FIFO_CONS) ? fd_p2c : fd_c2p;
        fwl->fd_out = (opts.flags == FW_FLAG_FIFO_CONS) ? fd_c2p : fd_p2c;
    } else if (opts.flags == FW_FLAG_VIRTIO) {
        fwl->fd_out = fwl->fd_in = open(opts.path, O_RDWR | O_NOCTTY);
        if (fwl->fd_in < 0) {
            perror("open");
            return -1;
        }
    } else {
        assert(opts.flags == FW_FLAG_SERIAL);
        fwl->fd_out = fwl->fd_in = open(opts.path, O_RDWR | O_NOCTTY | O_NDELAY);
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

    // prepare for input thread
    semaphore_init(&fwl->receive_wake);
    semaphore_init(&fwl->transmit_wake);
    fwl->rx_chart = data_rx;
    fwl->tx_chart = data_tx;

    chart_attach_client(data_rx, fakewire_link_notify_rx_chart, fwl);
    chart_attach_server(data_tx, fakewire_link_notify_tx_chart, fwl);

    // and now let's set up the input thread
    thread_create(&fwl->receive_thread,  "fw_rx_loop", PRIORITY_SERVERS, fakewire_link_rx_loop, fwl, NOT_RESTARTABLE);
    thread_create(&fwl->transmit_thread, "fw_tx_loop", PRIORITY_SERVERS, fakewire_link_tx_loop, fwl, NOT_RESTARTABLE);

    return 0;
}
