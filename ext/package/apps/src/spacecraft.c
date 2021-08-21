#include <assert.h>
#include <stdio.h>

#include "clock.h"
#include "cmd.h"
#include "debug.h"
#include "spacecraft.h"
#include "tlm.h"

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

static rmap_addr_t magnetometer_routing = {
    .destination = {
        .path_bytes = NULL,
        .num_path_bytes = 0,
        .logical_address = 42,
    },
    .source = {
        .path_bytes = NULL,
        .num_path_bytes = 0,
        .logical_address = 40,
    },
    .dest_key = 102,
};

static rmap_addr_t clock_routing = {
    .destination = {
        .path_bytes = NULL,
        .num_path_bytes = 0,
        .logical_address = 43,
    },
    .source = {
        .path_bytes = NULL,
        .num_path_bytes = 0,
        .logical_address = 40,
    },
    .dest_key = 103,
};

static bool initialized = false;
static spacecraft_t sc;

static void spacecraft_init(void) {
    assert(!initialized);

    // initialize fakewire infrastructure
    fakewire_exc_init(&sc.fwport, "rmap_io");
    int err = fakewire_exc_attach(&sc.fwport, "/dev/vport0p1", FW_FLAG_VIRTIO);
    assert(err == 0);
    rmap_init_monitor(&sc.monitor, &sc.fwport, 0x2000);

    // initialize telecomm infrastructure
    ringbuf_init(&sc.uplink_ring, 0x4000, 1);
    ringbuf_init(&sc.downlink_ring, 0x4000, 1);
    comm_dec_init(&sc.comm_decoder, &sc.uplink_ring);
    comm_enc_init(&sc.comm_encoder, &sc.downlink_ring);
    telemetry_init(&sc.comm_encoder);

    // initialize clock
    clock_init(&sc.monitor, &clock_routing);

    // initialize radio
    radio_init(&sc.radio, &sc.monitor, &radio_routing, &sc.uplink_ring, &sc.downlink_ring);

    // initialize magnetometer
    magnetometer_init(&sc.mag, &sc.monitor, &magnetometer_routing);

    // initialize heartbeats
    heartbeat_init(&sc.heart);

    initialized = true;
}

int main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

#ifndef __FREERTOS__
	freopen("/dev/console", "w", stdout);
	freopen("/dev/console", "w", stderr);
#endif

    debug0("Initializing...");

    spacecraft_init();

    debug0("Entering command main loop");

    cmd_mainloop(&sc);

    return 0;
}
