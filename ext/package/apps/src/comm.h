#ifndef APP_COMM_H
#define APP_COMM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ringbuf.h"

typedef struct {
    uint32_t cmd_tlm_id;
    uint64_t timestamp_ns;
    size_t   data_len;
    uint8_t *data_bytes;
} comm_packet_t;

typedef struct {
    ringbuf_t *uplink;
    uint8_t   *scratch_buffer;
    size_t     resume_start;
    size_t     resume_end;
    uint32_t   err_count;
} comm_dec_t;

typedef struct {
    ringbuf_t *downlink;
    uint8_t   *scratch_buffer;
} comm_enc_t;

void comm_dec_init(comm_dec_t *dec, ringbuf_t *uplink);
// NOTE: the byte array produced here will be reused on the next call
void comm_dec_decode(comm_dec_t *dec, comm_packet_t *out);

void comm_enc_init(comm_enc_t *enc, ringbuf_t *downlink);
void comm_enc_encode(comm_enc_t *enc, comm_packet_t *in);

#endif /* APP_COMM_H */
