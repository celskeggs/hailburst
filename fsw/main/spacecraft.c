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

static const rmap_addr_t radio_up_routing = {
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

static const rmap_addr_t radio_down_routing = {
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

static const rmap_addr_t magnetometer_routing = {
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

static const rmap_addr_t clock_routing = {
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

SWITCH_REGISTER(fce_vswitch);
CHART_REGISTER(fce_tx_chart, 0x1100, 2);
CHART_REGISTER(fce_rx_chart, 0x1100, 2);
static const fw_link_options_t exchange_options = {
    .label = "bus",
    .path  = "/dev/vport0p1",
    .flags = FW_FLAG_VIRTIO,
};
FAKEWIRE_EXCHANGE_REGISTER(fce_fw_exchange, exchange_options, fce_rx_chart, fce_tx_chart);

CLOCK_REGISTER(sc_clock, clock_routing, clock_rx, clock_tx);

RADIO_REGISTER(sc_radio, radio_up_routing,   radio_up_rx,   radio_up_tx,   UPLINK_STREAM_CAPACITY,
                         radio_down_routing, radio_down_rx, radio_down_tx, DOWNLINK_STREAM_CAPACITY,
                         sc.uplink_stream,   sc.downlink_stream);

MAGNETOMETER_REGISTER(sc_mag, magnetometer_routing, sc_mag_rx, sc_mag_tx);

COMMAND_REGISTER(sc_cmd, sc);

void spacecraft_init(void) {
    assert(!initialized);

    debugf(INFO, "Configuring virtual switch routes...");
    // add physical routes
    switch_add_route(&fce_vswitch, PADDR_RADIO, VPORT_LINK, false);
    switch_add_route(&fce_vswitch, PADDR_MAG, VPORT_LINK, false);
    switch_add_route(&fce_vswitch, PADDR_CLOCK, VPORT_LINK, false);
    // add virtual routes
    switch_add_route(&fce_vswitch, VADDR_RADIO_UP, VPORT_RADIO_UP, false);
    switch_add_route(&fce_vswitch, VADDR_RADIO_DOWN, VPORT_RADIO_DOWN, false);
    switch_add_route(&fce_vswitch, VADDR_MAG, VPORT_MAG, false);
    switch_add_route(&fce_vswitch, VADDR_CLOCK, VPORT_CLOCK, false);

    debugf(INFO, "Initializing link to spacecraft bus...");
    switch_add_port(&fce_vswitch, VPORT_LINK, &fce_rx_chart, &fce_tx_chart);

    debugf(INFO, "Initializing telecomm infrastructure...");
    stream_init(&sc.uplink_stream, UPLINK_STREAM_CAPACITY);
    stream_init(&sc.downlink_stream, DOWNLINK_STREAM_CAPACITY);
    comm_dec_init(&sc.comm_decoder, &sc.uplink_stream);
    comm_enc_init(&sc.comm_encoder, &sc.downlink_stream);
    telemetry_init(&sc.comm_encoder);

#ifdef CLOCK_EXISTS
    debugf(INFO, "Initializing clock...");
    switch_add_port(&fce_vswitch, VPORT_CLOCK, &clock_tx, &clock_rx);
#else
    (void) clock_routing;
#endif /* CLOCK_EXISTS */

    debugf(INFO, "Attaching radio...");
    switch_add_port(&fce_vswitch, VPORT_RADIO_UP, &radio_up_tx, &radio_up_rx);
    switch_add_port(&fce_vswitch, VPORT_RADIO_DOWN, &radio_down_tx, &radio_down_rx);

    debugf(INFO, "Attaching magnetometer...");
    switch_add_port(&fce_vswitch, VPORT_MAG, &sc_mag_tx, &sc_mag_rx);

    initialized = true;
}
