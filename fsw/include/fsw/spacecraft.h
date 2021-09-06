#ifndef APP_SPACECRAFT_H
#define APP_SPACECRAFT_H

#include <fsw/fakewire/exchange.h>
#include <fsw/fakewire/rmap.h>
#include <fsw/comm.h>
#include <fsw/heartbeat.h>
#include <fsw/magnetometer.h>
#include <fsw/radio.h>
#include <fsw/ringbuf.h>

typedef struct {
    // fakewire infrastructure
    fw_exchange_t  fwport;
    rmap_monitor_t monitor;

    // devices
    radio_t        radio;
    magnetometer_t mag;

    // services
    heartbeat_t    heart;

    // telecomm infrastructure
    ringbuf_t      uplink_ring;
    ringbuf_t      downlink_ring;
    comm_dec_t     comm_decoder;
    comm_enc_t     comm_encoder;
} spacecraft_t;

#endif /* APP_SPACECRAFT_H */
