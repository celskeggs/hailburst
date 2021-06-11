#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "app.h"
#include "fakewire_exc.h"
#include "rmap.h"
#include "radio.h"
#include "ringbuf.h"

static fw_exchange_t fwport;
static rmap_addr_t radio_routing = {
    .destination = {
        .path_bytes = NULL,
        .num_path_bytes = 0,
        .logical_address = 41,
    },
    .source = {
        .path_bytes = NULL,
        .num_path_bytes = 0,
        .logical_address = 40,
    },
    .dest_key = 101,
};
static rmap_monitor_t monitor;
static radio_t radio;
static ringbuf_t uplink, downlink;
static bool rmap_init = false;

void init_rmap_listener(void) {
    assert(!rmap_init);

    fakewire_exc_init(&fwport, "rmap_io");
    fakewire_exc_attach(&fwport, "/dev/ttyAMA1", FW_FLAG_SERIAL);
    rmap_init_monitor(&monitor, &fwport, /* max read length */ 4);
    ringbuf_init(&uplink, 0x4000);
    ringbuf_init(&downlink, 0x4000);
    radio_init(&radio, &monitor, &radio_routing, &uplink, &downlink);

    rmap_init = true;
}

void task_rmap_listener(void) {
    assert(rmap_init);
    uint8_t buffer[64];
    char fmtout[256];

    for (;;) {
        printf("APP: Waiting for uplink data...\n");
        size_t count = ringbuf_read(&uplink, buffer, sizeof(buffer), RB_BLOCKING);
        assert(count > 0 && count <= sizeof(buffer));
        int fi = 0;
        for (int i = 0; i < count; i++) {
            if (i > 0) {
                fmtout[fi++] = ' ';
            }
            fi += sprintf(&fmtout[fi], "%02x", buffer[i]);
        }
        printf("APP: Received %zu bytes of uplink data: {%s}\n", count, fmtout);
    }


/*
    rmap_status_t status;
    uint8_t data_buf[4];
    size_t data_len;
    for (;;) {
        data_len = sizeof(data_buf);
        printf("RMAP: Reading radio Magic Number...\n");
        status = rmap_read(&context, &radio_routing, RF_INCREMENT, 0x00, 0x0000, &data_len, data_buf);
        if (status == RS_OK && data_len == 4) {
            uint32_t decoded = (data_buf[0] << 24) | (data_buf[1] << 16) | (data_buf[2] << 8) | (data_buf[3] << 0);
            printf("RMAP: Read radio Magic Number: status=%03x, data_len=%zu, magic=%08x\n", status, data_len, decoded);
        } else {
            printf("RMAP: Could not read radio Magic Number: status=%03x, data_len=%zu\n", status, data_len);
        }
        sleep(1);
    }
*/
}