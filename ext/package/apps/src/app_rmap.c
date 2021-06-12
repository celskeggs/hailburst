#include <assert.h>
#include <inttypes.h>
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
#include "comm.h"

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
static comm_dec_t decoder;
static bool rmap_init = false;

void init_rmap_listener(void) {
    assert(!rmap_init);

    fakewire_exc_init(&fwport, "rmap_io");
    fakewire_exc_attach(&fwport, "/dev/ttyAMA1", FW_FLAG_SERIAL);
    rmap_init_monitor(&monitor, &fwport, 0x2000);
    ringbuf_init(&uplink, 0x4000, 1);
    ringbuf_init(&downlink, 0x4000, 1);
    radio_init(&radio, &monitor, &radio_routing, &uplink, &downlink);
    comm_dec_init(&decoder, &uplink);

    rmap_init = true;
}

void task_rmap_listener(void) {
    assert(rmap_init);
    char fmtout[4096];
    comm_packet_t packet;

    for (;;) {
        printf("APP: Waiting for next command...\n");
        comm_dec_decode(&decoder, &packet);
        printf("APP: Received command: cid=0x%08x, timestamp_ns=%"PRIu64", len=%zu:\n", packet.cmd_tlm_id, packet.timestamp_ns, packet.data_len);
        assert(packet.data_len < sizeof(fmtout) / 3);
        int fi = 0;
        for (int i = 0; i < packet.data_len; i++) {
            if (i > 0) {
                fmtout[fi++] = ' ';
            }
            fi += sprintf(&fmtout[fi], "%02x", packet.data_bytes[i]);
        }
        printf("APP: {%s}\n", fmtout);
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
