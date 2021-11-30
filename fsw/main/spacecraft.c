#include <hal/platform.h>
#include <hal/watchdog.h>
#include <fsw/clock.h>
#include <fsw/clock_init.h>
#include <fsw/cmd.h>
#include <fsw/debug.h>
#include <fsw/spacecraft.h>
#include <fsw/tlm.h>

enum {
    UPLINK_STREAM_CAPACITY = 0x4000,
    DOWNLINK_STREAM_CAPACITY = 0x4000,
};

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

    debugf(INFO, "Initializing fakewire infrastructure...");
    fw_link_options_t options = {
        .label = "bus",
        .path  = "/dev/vport0p1",
        .flags = FW_FLAG_VIRTIO,
    };
    int err = rmap_init_monitor(&sc.monitor, options, 0x2000);
    assert(err == 0);

    debugf(INFO, "Initializing telecomm infrastructure...");
    stream_init(&sc.uplink_stream, UPLINK_STREAM_CAPACITY);
    stream_init(&sc.downlink_stream, DOWNLINK_STREAM_CAPACITY);
    comm_dec_init(&sc.comm_decoder, &sc.uplink_stream);
    comm_enc_init(&sc.comm_encoder, &sc.downlink_stream);
    telemetry_init(&sc.comm_encoder);

    debugf(INFO, "Initializing clock...");
    clock_init(&sc.monitor, &clock_routing);

    debugf(INFO, "Initializing radio...");
    radio_init(&sc.radio, &sc.monitor, &radio_routing, &sc.uplink_stream, &sc.downlink_stream, DOWNLINK_STREAM_CAPACITY);

    debugf(INFO, "Initializing magnetometer...");
    magnetometer_init(&sc.mag, &sc.monitor, &magnetometer_routing);

    debugf(INFO, "Initializing heartbeats...");
    heartbeat_init(&sc.heart);

    debugf(INFO, "Initializing watchdog...");
    watchdog_init();

    initialized = true;
}

int main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    platform_init();

    debugf(CRITICAL, "Initializing...");

    spacecraft_init();

    debugf(INFO, "Entering command main loop");

    cmd_mainloop(&sc);

    return 0;
}
