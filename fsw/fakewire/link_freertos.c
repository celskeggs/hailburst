#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <fsw/debug.h>
#include <fsw/fakewire/link.h>

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

    int status = fakewire_enc_encode_data(&fwl->encoder, bytes_in, bytes_count);
    assert(status == 0); // make sure no data is dropped; FreeRTOS never shuts down
}

static void fakewire_link_recv_ctrl(void *opaque, fw_ctrl_t symbol, uint32_t param) {
    assert(opaque != NULL);
    assert(param == 0 || fakewire_is_parametrized(symbol));
    fw_link_t *fwl = (fw_link_t*) opaque;

#ifdef DEBUG
    debug_printf("Transmitting control character: %s(%u).", fakewire_codec_symbol(symbol), param);
#endif

    int status = fakewire_enc_encode_ctrl(&fwl->encoder, symbol, param);
    assert(status == 0); // make sure no data is dropped; FreeRTOS never shuts down
}

static void *fakewire_link_output_loop(void *opaque) {
    assert(opaque != NULL);
    fw_link_t *fwl = (fw_link_t*) opaque;

    char write_buf[FW_LINK_RING_SIZE];

    struct vector_entry entry = {
        .data_buffer = write_buf,
        .length      = 0 /* will be filled in later */,
        .is_receive  = false,
    };

    while (true) {
        // read as many bytes as possible from ring buffer in one chunk
        size_t count_bytes = ringbuf_read(&fwl->enc_ring, write_buf, sizeof(write_buf), RB_BLOCKING);
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

        // write one large chunk to the output port
#ifdef DEBUG
        debug_printf("Writing %zu bytes to VIRTIO port...", count_bytes);
#endif
        entry.length = count_bytes;
        ssize_t status = virtio_transact_sync(fwl->port->transmitq, &entry, 1);
        if (status != 0) {
            debug_printf("Write failed: status=%zd", status);
            return NULL;
        }
#ifdef DEBUG
        debug_printf("Finished writing data to VIRTIO port.", actual);
#endif
    }
    return NULL;
}

static void *fakewire_link_input_loop(void *opaque) {
    assert(opaque != NULL);
    fw_link_t *fwl = (fw_link_t*) opaque;

    uint8_t read_buf[1024];

    struct vector_entry entry = {
        .data_buffer = read_buf,
        .length      = sizeof(read_buf),
        .is_receive  = true,
    };

    while (true) {
        // read as many bytes as possible from the input port at once
        ssize_t actual = virtio_transact_sync(fwl->port->receiveq, &entry, 1);
        if (actual <= 0) {
            debug_printf("Read failed: %zd when maximum was %zu", actual, sizeof(read_buf));
            return NULL;
        }

#ifdef DEBUG
        debug_printf("Read %zd bytes from file descriptor.", actual);
#endif
        assert(actual > 0 && actual <= (ssize_t) sizeof(read_buf));

        // write as many bytes at once as possible
        fakewire_dec_decode(&fwl->decoder, read_buf, actual);
    }
    return NULL;
}

static void fakewire_link_setup(void *opaque, struct virtio_console_port *port) {
    assert(opaque != NULL && port != NULL);
    fw_link_t *fwl = (fw_link_t *) opaque;

    mutex_lock(&fwl->port_mutex);
    assert(fwl->port == NULL);
    fwl->port = port;
    BaseType_t status = xSemaphoreGive(fwl->port_acquired);
    assert(status == pdTRUE);
    mutex_unlock(&fwl->port_mutex);
}

int fakewire_link_init(fw_link_t *fwl, fw_receiver_t *receiver, const char *path, int flags, const char *label) {
    assert(fwl != NULL && receiver != NULL && path != NULL);
    assert(flags == FW_FLAG_VIRTIO); // only flags supported on FreeRTOS
    (void) path; // ignore path
    memset(fwl, 0, sizeof(fw_link_t));

    // set up debug info real quick
    fwl->label = label;
    mutex_init(&fwl->port_mutex);
    fwl->port_acquired = xSemaphoreCreateBinary();

    // first, let's open the file descriptors for the VIRTIO backend
    virtio_init(fakewire_link_setup, fwl);

    // port
    debug0("Waiting for VIRTIO port to be configured...");
    BaseType_t status = xSemaphoreTake(fwl->port_acquired, portMAX_DELAY);
    assert(status == pdTRUE);
    assert(fwl->port != NULL);
    debug0("VIRTIO port identified! Proceeding with fakewire initialization.");

    // next, let's configure all the data structures and interfaces
    fwl->interface = (fw_receiver_t) {
        .param = fwl,
        .recv_data = fakewire_link_recv_data,
        .recv_ctrl = fakewire_link_recv_ctrl,
    };
    ringbuf_init(&fwl->enc_ring, FW_LINK_RING_SIZE, 1);
    fakewire_enc_init(&fwl->encoder, &fwl->enc_ring);
    fakewire_dec_init(&fwl->decoder, receiver);

    // tell the serial port device that we're ready to receive
    virtio_serial_ready(fwl->port);

    // now let's start the I/O threads
    thread_create(&fwl->output_thread, "fw_out_loop", PRIORITY_SERVERS, fakewire_link_output_loop, fwl);
    thread_create(&fwl->input_thread, "fw_in_loop", PRIORITY_SERVERS, fakewire_link_input_loop, fwl);

    return 0;
}

fw_receiver_t *fakewire_link_interface(fw_link_t *fwl) {
    assert(fwl != NULL);

    return &fwl->interface;
}
