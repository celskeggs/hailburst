#include <assert.h>
#include <string.h>

#include "debug.h"
#include "fakewire_codec.h"

void fakewire_dec_init(fw_decoder_t *fwd, fw_receiver_t *output) {
    assert(fwd != NULL && output != NULL);
    fwd->output = output;
    fwd->parity_ok = true;
    fwd->stashed_bits = 0;
    fwd->stashed = 0;
}

void fakewire_dec_eof(fw_decoder_t *fwd) {
    if (fwd->parity_ok) {
        debug0("fakewire_dec_decode: hit parity failure on EOF");
        fwd->parity_ok = false;
        fwd->output->parity_fail(fwd->output->param);
    }
}

void fakewire_dec_decode(fw_decoder_t *fwd, uint8_t *bytes_in, size_t byte_count) {
    assert(byte_count > 0);
    // once parity fails, we can just discard everything else that comes in.
    if (!fwd->parity_ok) {
        return;
    }

    // restore previous stashed bits
    uint32_t curbits = fwd->stashed;
    uint8_t nbits = fwd->stashed_bits;
    assert(nbits == (nbits & 14));
    assert((curbits >> nbits) == 0);

    uint8_t databuf[256];
    size_t db_index = 0;

    for (size_t byte_i = 0; byte_i < byte_count; byte_i++) {
        curbits |= bytes_in[byte_i] << nbits;
        nbits += 8;

        int ctrl_bit;
        while (nbits >= ((ctrl_bit = curbits & 2) ? 6 : 12)) {
            // push data to receiver before each control bit, and also if the buffer fills up.
            if (ctrl_bit ? (db_index > 0) : (db_index == sizeof(databuf))) {
                fwd->output->recv_data(fwd->output->param, databuf, db_index);
                db_index = 0;
            }

            // check parity based on which bits are included for control or data characters
            if (!__builtin_parity(curbits & (ctrl_bit ? 0x3C : 0xFFC))) {
                // parity failure!
                debugf("fakewire_dec_decode: hit parity failure: bits=0x%x num=%u", curbits, nbits);
                fwd->parity_ok = false;
                fwd->output->parity_fail(fwd->output->param);
                return;
            }

            // decode control character or data character
            if (ctrl_bit) {
                // decode control character
                fw_ctrl_t symbol = (curbits >> 2) & 3;
                assert(FWC_FCT <= symbol && symbol <= FWC_ESC);
                fwd->output->recv_ctrl(fwd->output->param, symbol);

                // remove consumed bits
                nbits -= 4;
                curbits >>= 4;
            } else {
                // decode data character
                assert(db_index < sizeof(databuf));
                databuf[db_index++] = (uint8_t) (curbits >> 2);

                // remove consumed bits
                nbits -= 10;
                curbits >>= 10;
            }
        }
    }

    // if any data remains, push it to the receiver.
    if (db_index > 0) {
        fwd->output->recv_data(fwd->output->param, databuf, db_index);
    }

    // saved newly stashed bits
    assert(nbits == (nbits & 14));
    assert((curbits >> nbits) == 0);
    fwd->stashed_bits = nbits;
    fwd->stashed = curbits;
}

void fakewire_enc_init(fw_encoder_t *fwe, ringbuf_t *output) {
    assert(fwe != NULL && output != NULL);
    assert(ringbuf_elem_size(output) == 1);
    fwe->output = output;
    fwe->stashed_bits = 0;
    fwe->stashed = 0;
    fwe->last_remainder = 0; // (either initialization should be fine)
}

void fakewire_enc_encode_data(fw_encoder_t *fwe, uint8_t *bytes_in, size_t byte_count) {
    assert(fwe != NULL && bytes_in != NULL);
    assert(byte_count > 0);
    // allocate enough space for byte_count * 10 + 6 bits, plus a bit of margin
    uint8_t temp[byte_count + (byte_count >> 2) + 4];
    memset(temp, 0, sizeof(temp));

    // resume previous bits
    size_t bit_offset = fwe->stashed_bits;
    assert(bit_offset == (bit_offset & 6)); // 0, 2, 4, or 6 only
    assert((fwe->stashed >> bit_offset) == 0);
    temp[0] = fwe->stashed;

    // encode bits into temp buffer
    uint8_t lr = (fwe->last_remainder & 1);
    for (size_t i = 0; i < byte_count; i++) {
        uint8_t byte = bytes_in[i];

        // [last:odd] [P] [C=0] -> P must be 0 to be odd!
        // [last:even] [P] [C=0] -> P must be 1 to be odd!
        uint8_t parity_bit = (lr ^ 1);

        uint8_t *p8 = &temp[bit_offset >> 3];
        uint16_t bits = ((byte << 2) | parity_bit) << (bit_offset & 6);
        p8[0] |= (uint8_t) bits;
        p8[1] |= (uint8_t) (bits >> 8);
        bit_offset += 10;

        lr = __builtin_parity(byte);
    }
    fwe->last_remainder = lr;

    // extract bits that don't fit into a whole number of bytes
    fwe->stashed_bits = bit_offset & 0x6;
    fwe->stashed = temp[bit_offset >> 3];
    assert((fwe->stashed >> fwe->stashed_bits) == 0);

    // write data to ring buffer
    ringbuf_write_all(fwe->output, temp, bit_offset >> 3);
}

void fakewire_enc_encode_ctrl(fw_encoder_t *fwe, fw_ctrl_t symbol) {
    assert(fwe != NULL);
    assert(symbol >= FWC_FCT && symbol <= FWC_ESC);

    // [last:odd] [P] [C=1] -> P must be 1 to be odd!
    // [last:even] [P] [C=1] -> P must be 0 to be odd!
    int parity_bit = fwe->last_remainder & 1;

    uint8_t new_bits = ((symbol & 3) << 2) | (1 << 1) | parity_bit;

    // resume previous bits
    size_t bit_offset = fwe->stashed_bits;
    assert(bit_offset == (bit_offset & 6)); // 0, 2, 4, or 6 only
    assert((fwe->stashed >> bit_offset) == 0);

    // compute bit combination
    uint16_t temp = fwe->stashed | (new_bits << bit_offset);
    bit_offset += 4;

    // write to ring buffer if totals at least one byte
    if (bit_offset & 8) {
        uint8_t out = (uint8_t) temp;
        ringbuf_write_all(fwe->output, &out, 1);
    }

    // stash information to resume later
    fwe->stashed = (temp >> (bit_offset & 0x8));
    fwe->stashed_bits = bit_offset & 0x6;

    fwe->last_remainder = __builtin_parity(symbol & 3);
}
