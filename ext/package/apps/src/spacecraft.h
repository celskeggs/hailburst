#ifndef APP_SPACECRAFT_H
#define APP_SPACECRAFT_H

#include "comm.h"
#include "fakewire_exc.h"
#include "magnetometer.h"
#include "radio.h"
#include "ringbuf.h"
#include "rmap.h"

typedef struct {
    // fakewire infrastructure
    fw_exchange_t  fwport;
    rmap_monitor_t monitor;

    // devices
    radio_t        radio;
    magnetometer_t mag;

    // telecomm infrastructure
    ringbuf_t      uplink_ring;
    ringbuf_t      downlink_ring;
    comm_dec_t     comm_decoder;
    comm_enc_t     comm_encoder;
} spacecraft_t;

#endif /* APP_SPACECRAFT_H */
