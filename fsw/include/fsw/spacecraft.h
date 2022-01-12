#ifndef APP_SPACECRAFT_H
#define APP_SPACECRAFT_H

#include <fsw/fakewire/exchange.h>
#include <fsw/fakewire/rmap.h>
#include <fsw/comm.h>
#include <fsw/magnetometer.h>
#include <fsw/radio.h>

typedef struct {
    // devices
    radio_t        radio;
    magnetometer_t mag;

    // telecomm infrastructure
    stream_t       uplink_stream;
    stream_t       downlink_stream;
    comm_dec_t     comm_decoder;
    comm_enc_t     comm_encoder;
} spacecraft_t;

void spacecraft_init(void);

#endif /* APP_SPACECRAFT_H */
