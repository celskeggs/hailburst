#ifndef FSW_COMM_H
#define FSW_COMM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <hal/thread.h>
#include <synch/stream.h>

enum {
    COMM_SCRATCH_SIZE = 1024,
};

typedef struct {
    uint32_t       cmd_tlm_id;
    uint64_t       timestamp_ns;
    size_t         data_len;
    const uint8_t *data_bytes;
} comm_packet_t;

typedef struct {
    stream_t *uplink;
    uint8_t   scratch_buffer[COMM_SCRATCH_SIZE];
    size_t    resume_start;
    size_t    resume_end;
    uint32_t  err_count;
} comm_dec_t;

typedef struct {
    stream_t *downlink;
    uint8_t   scratch_buffer[COMM_SCRATCH_SIZE];
} comm_enc_t;

void comm_dec_init(comm_dec_t *dec, stream_t *uplink);
void comm_dec_set_task(comm_dec_t *dec, thread_t thread);
// NOTE: the byte array produced here will be reused on the next call
void comm_dec_decode(comm_dec_t *dec, comm_packet_t *out);

void comm_enc_init(comm_enc_t *enc, stream_t *downlink);
void comm_enc_set_task(comm_enc_t *enc, thread_t thread);
void comm_enc_encode(comm_enc_t *enc, comm_packet_t *in);

#endif /* FSW_COMM_H */
