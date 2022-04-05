#include <hal/debug.h>
#include <hal/system.h>
#include <hal/watchdog.h>
#include <bus/exchange.h>
#include <bus/switch.h>
#include <flight/clock.h>
#include <flight/clock_cal.h>
#include <flight/command.h>
#include <flight/heartbeat.h>
#include <flight/pingback.h>
#include <flight/radio.h>
#include <flight/spacecraft.h>
#include <flight/telemetry.h>

enum {
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

// the following line should have the max of all IO packet lengths... since radio is the largest right now, we'll just
// put that directly (to simplify the macros) and assert that we were right that it was largest, instead of trying to
// do a compile-time comparison.
enum {
    max_bus_packet = RADIO_MAX_IO_PACKET(UPLINK_BUF_LOCAL_SIZE, DOWNLINK_BUF_LOCAL_SIZE),
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

PIPE_REGISTER(sc_uplink_pipe,   1, COMMAND_REPLICAS, 1, UPLINK_BUF_LOCAL_SIZE,   PIPE_SENDER_FIRST);
PIPE_REGISTER(sc_downlink_pipe, 1, 1,                1, DOWNLINK_BUF_LOCAL_SIZE, PIPE_SENDER_FIRST);

RADIO_REGISTER(sc_radio, fce_vin, fce_vout,
               radio_up_routing,   VPORT_RADIO_UP,   UPLINK_BUF_LOCAL_SIZE,   sc_uplink_pipe,
               radio_down_routing, VPORT_RADIO_DOWN, DOWNLINK_BUF_LOCAL_SIZE, sc_downlink_pipe);

MAGNETOMETER_REGISTER(sc_mag, magnetometer_routing, fce_vin, fce_vout, VPORT_MAG);

HEARTBEAT_REGISTER(sc_heart);

PINGBACK_REGISTER(sc_pingback);

COMMAND_SYSTEM_REGISTER(sc_cmd, sc_uplink_pipe, {
    PINGBACK_COMMAND(sc_pingback)
    MAGNETOMETER_COMMAND(sc_mag)
});

TELEMETRY_SYSTEM_REGISTER(sc_telemetry, sc_downlink_pipe, {
    COMMAND_TELEMETRY(sc_cmd)
    MAGNETOMETER_TELEMETRY(sc_mag)
    CLOCK_TELEMETRY(sc_clock)
    PINGBACK_TELEMETRY(sc_pingback)
    HEARTBEAT_TELEMETRY(sc_heart)
});

WATCHDOG_REGISTER(sc_watchdog, {
    SYSTEM_MAINTENANCE_WATCH()
    HEARTBEAT_WATCH(sc_heart)
    RADIO_WATCH(sc_radio)
    TELEMETRY_WATCH(sc_telemetry)
});

TASK_SCHEDULING_ORDER(
    FAKEWIRE_EXCHANGE_SCHEDULE(fce_fw_exchange)
    SWITCH_SCHEDULE(fce_vin)
    RADIO_UP_SCHEDULE(sc_radio)
    COMMAND_SCHEDULE(sc_cmd)
    MAGNETOMETER_SCHEDULE(sc_mag)
    CLOCK_SCHEDULE(sc_clock)
    PINGBACK_SCHEDULE(sc_pingback)
    HEARTBEAT_SCHEDULE(sc_heart)
    TELEMETRY_SCHEDULE(sc_telemetry)
    RADIO_DOWN_SCHEDULE(sc_radio)
    SWITCH_SCHEDULE(fce_vout)
    SYSTEM_MAINTENANCE_SCHEDULE()
    WATCHDOG_SCHEDULE(sc_watchdog)
);
