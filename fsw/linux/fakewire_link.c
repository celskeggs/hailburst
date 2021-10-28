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

enum {
    FW_LINK_RING_SIZE = 1024,
};

//#define DEBUG

#define debug_puts(str) (debugf("[ fakewire_link] [%s] %s", fwl->label, str))
#define debug_printf(fmt, ...) (debugf("[ fakewire_link] [%s] " fmt, fwl->label, __VA_ARGS__))

void fakewire_link_write(fw_link_t *fwl, uint8_t *bytes_in, size_t bytes_count) {
    assert(fwl != NULL && bytes_in != NULL && bytes_count > 0);

    // write one large chunk to the output port
#ifdef DEBUG
    debug_printf("Writing %zu bytes to file descriptor...", bytes_count);
#endif
    ssize_t actual = write(fwl->fd_out, bytes_in, bytes_count);
    if (actual == (ssize_t) bytes_count) {
#ifdef DEBUG
        debug_printf("Finished writing %zd bytes to file descriptor.", actual);
#endif
    } else {
        debug_printf("Write failed: %zd bytes instead of %zu bytes", actual, bytes_count);
    }
}

static void *fakewire_link_input_loop(void *opaque) {
    assert(opaque != NULL);
    fw_link_t *fwl = (fw_link_t*) opaque;

    while (true) {
        struct io_rx_ent *entry = chart_ack_start(fwl->rx_chart);
        if (entry != NULL) {
            chart_ack_send(fwl->rx_chart, entry);
            continue;
        }
        entry = chart_request_start(fwl->rx_chart);
        if (entry == NULL) {
            // wait for another entry to be available
            semaphore_take(&fwl->input_wake);
            continue;
        }
        // read as many bytes as possible from the input port at once
        size_t max = chart_note_size(fwl->rx_chart) - offsetof(struct io_rx_ent, data);
        ssize_t actual = read(fwl->fd_in, entry->data, max);

        if (actual <= 0) { // 0 means EOF, <0 means error
            debug_printf("Read failed: %zd when maximum was %zu", actual, max);
            return NULL;
        }
#ifdef DEBUG
        debug_printf("Read %zd bytes from file descriptor.", actual);
#endif
        assert(actual > 0 && actual <= (ssize_t) max);

        entry->receive_timestamp = clock_timestamp();
        entry->actual_length = (uint32_t) actual;

        chart_request_send(fwl->rx_chart, entry);
    }
}

void fakewire_link_notify_rx_chart(fw_link_t *fwl) {
    assert(fwl != NULL);

    // we don't worry about wakeups getting dropped.
    // that just means a previous wakeup is still pending, which is just as good!
    (void) semaphore_give(&fwl->input_wake);
}

int fakewire_link_init(fw_link_t *fwl, fw_link_options_t opts, chart_t *data_rx) {
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
    semaphore_init(&fwl->input_wake);
    fwl->rx_chart = data_rx;

    // and now let's set up the input thread
    thread_create(&fwl->input_thread, "fw_in_loop", PRIORITY_SERVERS, fakewire_link_input_loop, fwl, NOT_RESTARTABLE);

    return 0;
}
