#ifndef APP_SPACECRAFT_H
#define APP_SPACECRAFT_H

#include <fsw/fakewire/exchange.h>
#include <fsw/fakewire/rmap.h>
#include <fsw/comm.h>
#include <fsw/heartbeat.h>
#include <fsw/magnetometer.h>
#include <fsw/radio.h>

typedef struct {
    // fakewire infrastructure
    rmap_monitor_t monitor;

    // devices
    radio_t        radio;
    magnetometer_t mag;

    // services
    heartbeat_t    heart;

    // telecomm infrastructure
    stream_t       uplink_stream;
    stream_t       downlink_stream;
    comm_dec_t     comm_decoder;
    comm_enc_t     comm_encoder;
} spacecraft_t;

#endif /* APP_SPACECRAFT_H */
