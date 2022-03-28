#ifndef FSW_COMM_H
#define FSW_COMM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <hal/thread.h>
#include <synch/pipebuf.h>

enum {
    COMM_SCRATCH_SIZE = 0x1000,
};

typedef struct {
    uint32_t       cmd_tlm_id;
    uint64_t       timestamp_ns;
    size_t         data_len;
    const uint8_t *data_bytes;
} comm_packet_t;

typedef struct {
    pipe_receiver_t *uplink;
    uint8_t   decode_buffer[COMM_SCRATCH_SIZE];
    bool      decode_in_progress;
    size_t    decode_offset;
    uint32_t  err_count;
} comm_dec_t;

typedef struct {
    pipe_sender_t   *downlink;
} comm_enc_t;

macro_define(COMM_DEC_REGISTER, d_ident, d_uplink, d_replica) {
    PIPE_RECEIVER_REGISTER(symbol_join(d_ident, receiver), d_uplink, COMM_SCRATCH_SIZE, d_replica);
    comm_dec_t d_ident = {
        .uplink = &symbol_join(d_ident, receiver),
        .decode_buffer = { 0 },
        .decode_in_progress = false,
        .decode_offset = 0,
        .err_count = 0,
    }
}

void comm_dec_reset(comm_dec_t *dec);
void comm_dec_prepare(comm_dec_t *dec);
// NOTE: the byte array produced here will be reused on the next call
bool comm_dec_decode(comm_dec_t *dec, comm_packet_t *out);
void comm_dec_commit(comm_dec_t *dec);

macro_define(COMM_ENC_REGISTER, e_ident, e_downlink, e_replica) {
    PIPE_SENDER_REGISTER(symbol_join(e_ident, sender), e_downlink, COMM_SCRATCH_SIZE, e_replica);
    comm_enc_t e_ident = {
        .downlink = &symbol_join(e_ident, sender),
    }
}

void comm_enc_reset(comm_enc_t *enc);
void comm_enc_prepare(comm_enc_t *enc);
bool comm_enc_encode(comm_enc_t *enc, comm_packet_t *in);
void comm_enc_commit(comm_enc_t *enc);

#endif /* FSW_COMM_H */
