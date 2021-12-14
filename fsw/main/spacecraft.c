#include <hal/watchdog.h>
#include <fsw/clock.h>
#include <fsw/clock_init.h>
#include <fsw/command.h>
#include <fsw/debug.h>
#include <fsw/spacecraft.h>
#include <fsw/telemetry.h>

enum {
    UPLINK_STREAM_CAPACITY = 0x4000,
    DOWNLINK_STREAM_CAPACITY = 0x4000,

    // physical component addresses
    PADDR_RADIO = 45,
    PADDR_MAG   = 46,
    PADDR_CLOCK = 47,

    // port numbers on the virtual switch
    VPORT_LINK       = 1,
    VPORT_RADIO_UP   = 2,
    VPORT_RADIO_DOWN = 3,
    VPORT_MAG        = 4,
    VPORT_CLOCK      = 5,

    // FSW component addresses; in the range of addresses routed to the FCE by the physical switch
    VADDR_RADIO_UP   = 32,
    VADDR_RADIO_DOWN = 33,
    VADDR_MAG        = 34,
    VADDR_CLOCK      = 35,
};

static rmap_addr_t radio_up_routing = {
    .destination = {
        .path_bytes = NULL,
        .num_path_bytes = 0,
        .logical_address = PADDR_RADIO,
    },
    .source = {
        .path_bytes = NULL,
        .num_path_bytes = 0,
        .logical_address = VADDR_RADIO_UP,
    },
    .dest_key = 101,
};

static rmap_addr_t radio_down_routing = {
    .destination = {
        .path_bytes = NULL,
        .num_path_bytes = 0,
        .logical_address = PADDR_RADIO,
    },
    .source = {
        .path_bytes = NULL,
        .num_path_bytes = 0,
        .logical_address = VADDR_RADIO_DOWN,
    },
    .dest_key = 101,
};

static rmap_addr_t magnetometer_routing = {
    .destination = {
        .path_bytes = NULL,
        .num_path_bytes = 0,
        .logical_address = PADDR_MAG,
    },
    .source = {
        .path_bytes = NULL,
        .num_path_bytes = 0,
        .logical_address = VADDR_MAG,
    },
    .dest_key = 102,
};

static rmap_addr_t clock_routing = {
    .destination = {
        .path_bytes = NULL,
        .num_path_bytes = 0,
        .logical_address = PADDR_CLOCK,
    },
    .source = {
        .path_bytes = NULL,
        .num_path_bytes = 0,
        .logical_address = VADDR_CLOCK,
    },
    .dest_key = 103,
};

static bool initialized = false;
static spacecraft_t sc;

void spacecraft_init(void) {
    assert(!initialized);

    debugf(INFO, "Initializing virtual switch...");
    switch_init(&sc.vswitch);
    // add physical routes
    switch_add_route(&sc.vswitch, PADDR_RADIO, VPORT_LINK, false);
    switch_add_route(&sc.vswitch, PADDR_MAG, VPORT_LINK, false);
    switch_add_route(&sc.vswitch, PADDR_CLOCK, VPORT_LINK, false);
    // add virtual routes
    switch_add_route(&sc.vswitch, VADDR_RADIO_UP, VPORT_RADIO_UP, false);
    switch_add_route(&sc.vswitch, VADDR_RADIO_DOWN, VPORT_RADIO_DOWN, false);
    switch_add_route(&sc.vswitch, VADDR_MAG, VPORT_MAG, false);
    switch_add_route(&sc.vswitch, VADDR_CLOCK, VPORT_CLOCK, false);

    debugf(INFO, "Initializing link to spacecraft bus...");
    fw_link_options_t options = {
        .label = "bus",
        .path  = "/dev/vport0p1",
        .flags = FW_FLAG_VIRTIO,
    };
    chart_init(&sc.etx_chart, 0x1100, 2);
    chart_init(&sc.erx_chart, 0x1100, 2);
    int err = fakewire_exc_init(&sc.exchange, options, &sc.erx_chart, &sc.etx_chart);
    assert(err == 0);
    switch_add_port(&sc.vswitch, VPORT_LINK, &sc.erx_chart, &sc.etx_chart);

    debugf(INFO, "Initializing telecomm infrastructure...");
    stream_init(&sc.uplink_stream, UPLINK_STREAM_CAPACITY);
    stream_init(&sc.downlink_stream, DOWNLINK_STREAM_CAPACITY);
    comm_dec_init(&sc.comm_decoder, &sc.uplink_stream);
    comm_enc_init(&sc.comm_encoder, &sc.downlink_stream);
    telemetry_init(&sc.comm_encoder);

    debugf(INFO, "Initializing clock...");
    chart_t *clock_rx = NULL, *clock_tx = NULL;
    clock_init(&clock_routing, &clock_rx, &clock_tx);
    // we need to check, because the clock driver is only necessary on Linux right now, not on FreeRTOS.
    if (clock_rx != NULL || clock_tx != NULL) {
        switch_add_port(&sc.vswitch, VPORT_CLOCK, clock_tx, clock_rx);
    }

    clock_start();

    debugf(INFO, "Initializing radio...");
    chart_t *radio_up_rx = NULL, *radio_up_tx = NULL, *radio_down_rx = NULL, *radio_down_tx = NULL;
    radio_init(&sc.radio,
               &radio_up_routing, &radio_up_rx, &radio_up_tx, UPLINK_STREAM_CAPACITY,
               &radio_down_routing, &radio_down_rx, &radio_down_tx, DOWNLINK_STREAM_CAPACITY,
               &sc.uplink_stream, &sc.downlink_stream);
    switch_add_port(&sc.vswitch, VPORT_RADIO_UP, radio_up_tx, radio_up_rx);
    switch_add_port(&sc.vswitch, VPORT_RADIO_DOWN, radio_down_tx, radio_down_rx);

    debugf(INFO, "Initializing magnetometer...");
    chart_t *mag_rx = NULL, *mag_tx = NULL;
    magnetometer_init(&sc.mag, &magnetometer_routing, &mag_rx, &mag_tx);
    switch_add_port(&sc.vswitch, VPORT_MAG, mag_tx, mag_rx);

    debugf(INFO, "Initializing heartbeats...");
    heartbeat_init(&sc.heart);

    debugf(INFO, "Initializing watchdog...");
    watchdog_init();

    debugf(INFO, "Initializing command loop...");
    command_init(&sc);

    initialized = true;
}
