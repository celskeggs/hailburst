#include <hal/clock.h>
#include <hal/clock_init.h>
#include <hal/debug.h>
#include <hal/system.h>
#include <hal/watchdog.h>
#include <bus/switch.h>
#include <flight/command.h>
#include <flight/heartbeat.h>
#include <flight/spacecraft.h>
#include <flight/telemetry.h>

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

#ifdef CLOCK_EXISTS
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
#endif

static bool initialized = false;
static spacecraft_t sc;

// the following line should have the max of all IO packet lengths... since radio is the largest right now, we'll just
// put that directly (to simplify the macros) and assert that we were right that it was largest, instead of trying to
// do a compile-time comparison.
enum {
    max_bus_packet = RADIO_MAX_IO_PACKET(UPLINK_STREAM_CAPACITY, DOWNLINK_STREAM_CAPACITY),
};
static_assert(max_bus_packet >= MAGNETOMETER_MAX_IO_PACKET, "assumption violated about relative sizes of packets");
static_assert(max_bus_packet >= CLOCK_MAX_IO_PACKET, "assumption violated about relative sizes of packets");
SWITCH_REGISTER(fce_vout, max_bus_packet);
SWITCH_REGISTER(fce_vin,  max_bus_packet);

// add physical routes
SWITCH_ROUTE(fce_vout, PADDR_RADIO, VPORT_LINK, false);
SWITCH_ROUTE(fce_vout, PADDR_MAG, VPORT_LINK, false);
SWITCH_ROUTE(fce_vout, PADDR_CLOCK, VPORT_LINK, false);
// add virtual routes
SWITCH_ROUTE(fce_vin, VADDR_RADIO_UP, VPORT_RADIO_UP, false);
SWITCH_ROUTE(fce_vin, VADDR_RADIO_DOWN, VPORT_RADIO_DOWN, false);
SWITCH_ROUTE(fce_vin, VADDR_MAG, VPORT_MAG, false);
SWITCH_ROUTE(fce_vin, VADDR_CLOCK, VPORT_CLOCK, false);

static const fw_link_options_t exchange_options = {
    .label = "bus",
    .path  = "/dev/vport0p1",
    .flags = FW_FLAG_VIRTIO,
};
FAKEWIRE_EXCHANGE_ON_SWITCHES(fce_fw_exchange, exchange_options, fce_vin, fce_vout, VPORT_LINK,
                              RADIO_MAX_IO_FLOW + MAGNETOMETER_MAX_IO_FLOW + CLOCK_MAX_IO_FLOW, max_bus_packet);

CLOCK_REGISTER(sc_clock, clock_routing, fce_vin, fce_vout, VPORT_CLOCK);

STREAM_REGISTER(sc_uplink_stream, UPLINK_STREAM_CAPACITY);
STREAM_REGISTER(sc_downlink_stream, DOWNLINK_STREAM_CAPACITY);

RADIO_REGISTER(sc_radio, fce_vin, fce_vout,
               radio_up_routing,   VPORT_RADIO_UP,   UPLINK_STREAM_CAPACITY,   sc_uplink_stream,
               radio_down_routing, VPORT_RADIO_DOWN, DOWNLINK_STREAM_CAPACITY, sc_downlink_stream);

MAGNETOMETER_REGISTER(sc_mag, magnetometer_routing, fce_vin, fce_vout, VPORT_MAG);

COMMAND_REGISTER(sc_cmd, sc);

TASK_SCHEDULING_ORDER(
    FAKEWIRE_EXCHANGE_SCHEDULE(fce_fw_exchange)
    SWITCH_SCHEDULE(fce_vin)
    RADIO_UP_SCHEDULE(sc_radio)
    COMMAND_SCHEDULE(sc_cmd)
    MAGNETOMETER_SCHEDULE(sc_mag)
    TELEMETRY_SCHEDULE()
    CLOCK_SCHEDULE(sc_clock)
    HEARTBEAT_SCHEDULE()
    RADIO_DOWN_SCHEDULE(sc_radio)
    SWITCH_SCHEDULE(fce_vout)
    SYSTEM_MAINTENANCE_SCHEDULE()
);

void spacecraft_init(void) {
    assert(!initialized);

    debugf(INFO, "Initializing telecomm infrastructure...");
    comm_dec_init(&sc.comm_decoder, &sc_uplink_stream);
    comm_enc_init(&sc.comm_encoder, &sc_downlink_stream);
    telemetry_init(&sc.comm_encoder);

    initialized = true;
}
