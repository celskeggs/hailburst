#ifndef APP_SPACECRAFT_H
#define APP_SPACECRAFT_H

#include <bus/exchange.h>
#include <bus/rmap.h>
#include <flight/comm.h>
#include <flight/magnetometer.h>
#include <flight/radio.h>

typedef struct {
    // devices
    radio_t        radio;

    // telecomm infrastructure
    comm_dec_t     comm_decoder;
    comm_enc_t     comm_encoder;
} spacecraft_t;

extern magnetometer_t sc_mag;

void spacecraft_init(void);

#endif /* APP_SPACECRAFT_H */
