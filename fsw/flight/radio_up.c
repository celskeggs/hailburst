#include <endian.h>
#include <stdlib.h>
#include <string.h>

#include <hal/debug.h>
#include <flight/radio.h>

// #define DEBUGIDX

enum {
    RX_STATE_IDLE      = 0x00,
    RX_STATE_LISTENING = 0x01,
    RX_STATE_OVERFLOW  = 0x02,
};

const radio_memregion_t rx_halves[] = {
    [0] = { .base = 0,                  .size = RADIO_MEM_SIZE / 4 },
    [1] = { .base = RADIO_MEM_SIZE / 4, .size = RADIO_MEM_SIZE / 4 },
};

/*************************************************************************************************
 * The big challenge with radio reception is that we need to be able to CONTINUOUSLY receive     *
 * data from the ground, even if we're currently transferring part of the buffer to the FSW.     *
 * In order to support this, the radio implementation provides a pair of RX buffer pointers and  *
 * lengths; implementing a ring buffer would be difficult, but we can have a active/passive      *
 * buffering arrangement without too much trouble.                                               *
 *************************************************************************************************/

static void radio_uplink_compute_reads(radio_uplink_replica_t *rur, struct radio_uplink_note *mut_synch,
                                       uint32_t reg[NUM_REGISTERS], struct radio_uplink_reads *reads) {
    assert(rur != NULL && reg != NULL && reads != NULL);

    if (reg[REG_RX_STATE] == RX_STATE_IDLE) {
        debugf(INFO, "Radio: initializing uplink out of IDLE mode");

        mut_synch->bytes_extracted = 0;
        reg[REG_RX_PTR] = rx_halves[0].base;
        reg[REG_RX_LEN] = rx_halves[0].size;
        reg[REG_RX_PTR_ALT] = rx_halves[1].base;
        reg[REG_RX_LEN_ALT] = rx_halves[1].size;
        reg[REG_RX_STATE] = RX_STATE_LISTENING;

        // no data to read; just initialize the buffers
        *reads = (struct radio_uplink_reads) {
            .prime_read_length = 0,
            .flipped_read_length = 0,
            .needs_update_all = true,
            .watchdog_ok = false,
        };

        memcpy(reads->new_registers, &reg[REG_RX_PTR], sizeof(reads->new_registers));

#ifdef DEBUGIDX
        debugf(TRACE, "Radio UPDATED indices: end_index_prime=%u, end_index_alt=%u",
               reg[REG_RX_PTR] + reg[REG_RX_LEN], reg[REG_RX_PTR_ALT] + reg[REG_RX_LEN_ALT]);
#endif

        return;
    }

    // start by identifying what the current positions mean.
    const uint32_t end_index_h0 = rx_halves[0].base + rx_halves[0].size;
    const uint32_t end_index_h1 = rx_halves[1].base + rx_halves[1].size;

    uint32_t end_index_prime = reg[REG_RX_PTR] + reg[REG_RX_LEN];
    uint32_t end_index_alt = reg[REG_RX_PTR_ALT] + reg[REG_RX_LEN_ALT];
#ifdef DEBUGIDX
    debugf(TRACE, "Radio indices: end_index_h0=%u, end_index_h1=%u, end_index_prime=%u, end_index_alt=%u, extracted=%u",
           end_index_h0, end_index_h1, end_index_prime, end_index_alt, mut_synch->bytes_extracted);
#endif
    assert(end_index_prime == end_index_h0 || end_index_prime == end_index_h1);
    assert(end_index_prime != end_index_alt);
    if (end_index_alt == 0) {
        assert(reg[REG_RX_PTR_ALT] == 0 && reg[REG_RX_LEN_ALT] == 0);
    } else {
        assert(end_index_alt == end_index_h0 || end_index_alt == end_index_h1);
    }

    // identify where the next read location should be...
    uint32_t read_cycle_offset = mut_synch->bytes_extracted % (rx_halves[0].size + rx_halves[1].size);
    int read_half = (read_cycle_offset >= rx_halves[0].size) ? 1 : 0;
    uint32_t read_half_offset = read_cycle_offset - (read_half ? rx_halves[0].size : 0);

    uint32_t read_length; // bytes to read from current read half
    uint32_t read_length_flip; // bytes to read from opposite read half

    if (end_index_alt == 0) {
        // then we WERE in the non-prime half, and switched, which means the read index MUST be in the non-prime half
        if (end_index_prime == end_index_h0) {
            assert(read_half == 1);
        } else /* end_index_prime == end_index_h1 */ {
            assert(read_half == 0);
        }
        read_length = rx_halves[read_half].size - read_half_offset;
        read_length_flip = reg[REG_RX_PTR] - rx_halves[read_half ? 0 : 1].base;
    } else {
        // then we ARE in the prime half, and the read index must be here
        if (end_index_prime == end_index_h0) {
            assert(read_half == 0);
        } else /* end_index_prime == end_index_h1 */ {
            assert(read_half == 1);
        }
        read_length = (reg[REG_RX_PTR] - rx_halves[read_half].base) - read_half_offset;
        read_length_flip = 0;
    }
    assert(read_half_offset + read_length <= rx_halves[read_half].size);
    assert(read_length_flip <= rx_halves[read_half ? 0 : 1].size);

    // constrain the read to the actual size of the temporary buffer
    if (read_length > UPLINK_BUF_LOCAL_SIZE) {
        read_length = UPLINK_BUF_LOCAL_SIZE;
        read_length_flip = 0;
    } else if (read_length + read_length_flip > UPLINK_BUF_LOCAL_SIZE) {
        read_length_flip = UPLINK_BUF_LOCAL_SIZE - read_length;
    }

    // should not have read_length_flip nonzero when read_length is zero
    assert(read_length_flip == 0 || read_length != 0);

    *reads = (struct radio_uplink_reads) {
        .prime_read_address = rx_halves[read_half].base + read_half_offset,
        .prime_read_length = read_length,
        .flipped_read_address = rx_halves[read_half ? 0 : 1].base,
        .flipped_read_length = read_length_flip,
        .needs_update_all = false, /* updated later if necessary */
        .needs_alt_update = false, /* updated later if necessary */
        .watchdog_ok = true,
    };

    uint32_t total_read = read_length + read_length_flip;
    mut_synch->bytes_extracted += total_read;

    // quick coherency check: if we are in OVERFLOW condition, then we must have run out of data on our prime buffer.
    if (reg[REG_RX_STATE] == RX_STATE_OVERFLOW) {
        assert(reg[REG_RX_LEN] == 0);
    }

    // new question: is there any unread data in the alternate half?
    uint32_t reread_cycle_offset = mut_synch->bytes_extracted % (rx_halves[0].size + rx_halves[1].size);
    int reread_half = (reread_cycle_offset >= rx_halves[0].size) ? 1 : 0;

    bool any_unread_data_in_alternate = (reread_half == 0 && end_index_prime == end_index_h1)
                                     || (reread_half == 1 && end_index_prime == end_index_h0);

#ifdef DEBUGIDX
    debugf(TRACE, "Unread stats: bytes_extracted=%u, reread_half=%d, a_u_d_i_a=%d, ptr=%u, ptr_alt=%u",
           mut_synch->bytes_extracted, reread_half, any_unread_data_in_alternate,
           reg[REG_RX_PTR], reg[REG_RX_PTR_ALT]);
#endif

    if (any_unread_data_in_alternate) {
        // then we CANNOT safely have the alternate pointer and length set! we will have to finish reading.
        assert(end_index_alt == 0);
    } else {
        // then we CAN safely refill the alternate pointer and length.
        radio_memregion_t new_region = (end_index_prime == end_index_h1) ? rx_halves[0] : rx_halves[1];
        if (reg[REG_RX_STATE] == RX_STATE_OVERFLOW) {
            // simulate effect of flip
            reg[REG_RX_PTR] = new_region.base;
            reg[REG_RX_LEN] = new_region.size;
            reg[REG_RX_PTR_ALT] = 0;
            reg[REG_RX_LEN_ALT] = 0;
            reg[REG_RX_STATE] = RX_STATE_LISTENING;
            debugf(CRITICAL, "Radio: uplink OVERFLOW condition hit; clearing and resuming uplink.");

            memcpy(reads->new_registers, &reg[REG_RX_PTR], sizeof(reads->new_registers));

#ifdef DEBUGIDX
            debugf(TRACE, "Radio UPDATED indices: end_index_prime=%u, end_index_alt=%u",
                   reg[REG_RX_PTR] + reg[REG_RX_LEN], reg[REG_RX_PTR_ALT] + reg[REG_RX_LEN_ALT]);
#endif
            reads->needs_update_all = true;
        } else if (end_index_alt == 0) {
            // we need to refill the alternate pointer and length
            assert(reg[REG_RX_STATE] == RX_STATE_LISTENING);
            reg[REG_RX_PTR_ALT] = new_region.base;
            reg[REG_RX_LEN_ALT] = new_region.size;

            memcpy(reads->new_registers, &reg[REG_RX_PTR], sizeof(reads->new_registers));
#ifdef DEBUGIDX
            debugf(TRACE, "Radio UPDATED indices: end_index_prime=<unchanged>, end_index_alt=%u",
                   reg[REG_RX_PTR_ALT] + reg[REG_RX_LEN_ALT]);
#endif
            reads->needs_alt_update = true;
        } else {
            // or, in this case, no refill is actually necessary!
        }
    }
}

void radio_uplink_clip(radio_uplink_replica_t *rur) {
    assert(rur != NULL && rur->mut != NULL);

    rmap_status_t status;
    uint32_t registers[NUM_REGISTERS];

    bool valid = false;
    struct radio_uplink_note *mut_synch = notepad_feedforward(rur->mut_synch, &valid);

    if (!valid || (uint32_t) mut_synch->uplink_state > (uint32_t) RAD_UL_WRITE_TO_STREAM) {
        mut_synch->uplink_state = RAD_UL_INITIAL_STATE;
        mut_synch->bytes_extracted = 0;
        rmap_synch_reset(&mut_synch->rmap_synch);
    }

    rmap_txn_t rmap_txn;
    rmap_epoch_prepare(&rmap_txn, rur->rmap_up, &mut_synch->rmap_synch);

    bool watchdog_ok = false;

    switch (mut_synch->uplink_state) {
    case RAD_UL_QUERY_COMMON_CONFIG:
        status = rmap_read_complete(&rmap_txn, (uint8_t*) registers, sizeof(uint32_t) * 3, NULL);
        if (status == RS_OK) {
            for (int i = 0; i < 3; i++) {
                registers[i] = be32toh(registers[i]);
            }
            if (radio_validate_common_config(registers)) {
                mut_synch->uplink_state = RAD_UL_DISABLE_RECEIVE;
            } else {
                // invalid radio; stop.
            }
        } else {
            debugf(WARNING, "Failed to read initial radio metadata, error=0x%03x", status);
        }
        break;
    case RAD_UL_DISABLE_RECEIVE:
        status = rmap_write_complete(&rmap_txn, NULL);
        if (status == RS_OK) {
            mut_synch->uplink_state = RAD_UL_RESET_REGISTERS;
        } else {
            debugf(WARNING, "Failed to disable radio receiver, error=0x%03x", status);
        }
        break;
    case RAD_UL_RESET_REGISTERS:
        status = rmap_write_complete(&rmap_txn, NULL);
        if (status == RS_OK) {
            mut_synch->uplink_state = RAD_UL_QUERY_STATE;
        } else {
            debugf(WARNING, "Failed to reset radio receiver to known state, error=0x%03x", status);
        }
        break;
    case RAD_UL_QUERY_STATE:
        status = rmap_read_complete(&rmap_txn, (uint8_t*) (registers + REG_RX_PTR), sizeof(uint32_t) * 5, NULL);
        if (status == RS_OK) {
            for (int i = REG_RX_PTR; i < REG_RX_PTR + 5; i++) {
                registers[i] = be32toh(registers[i]);
            }
            radio_uplink_compute_reads(rur, mut_synch, registers, &mut_synch->read_plan);
            mut_synch->uplink_state = RAD_UL_PRIME_READ;
            flag_recoverf(&rur->mut->uplink_query_status_flag, "Radio status queries recovered.");
            watchdog_ok = mut_synch->read_plan.watchdog_ok;
        } else {
            flag_raisef(&rur->mut->uplink_query_status_flag, "Failed to query radio status, error=0x%03x", status);
        }
        break;
    case RAD_UL_PRIME_READ:
        status = rmap_read_complete(&rmap_txn, rur->mut->uplink_buf_local,
                                               mut_synch->read_plan.prime_read_length, NULL);
        if (status == RS_OK) {
            mut_synch->uplink_state = RAD_UL_FLIPPED_READ;
        } else {
            debugf(WARNING, "Failed to read prime memory region, error=0x%03x", status);
        }
        break;
    case RAD_UL_FLIPPED_READ:
        status = rmap_read_complete(&rmap_txn, rur->mut->uplink_buf_local + mut_synch->read_plan.prime_read_length,
                                               mut_synch->read_plan.flipped_read_length, NULL);
        if (status == RS_OK) {
            mut_synch->uplink_state = RAD_UL_REFILL_BUFFERS;
        } else {
            debugf(WARNING, "Failed to read flipped memory region, error=0x%03x", status);
        }
        break;
    case RAD_UL_REFILL_BUFFERS:
        status = rmap_write_complete(&rmap_txn, NULL);
        if (status == RS_OK) {
            mut_synch->uplink_state = RAD_UL_WRITE_TO_STREAM;
        } else {
            debugf(WARNING, "Failed to refill receiver buffers, error=0x%03x", status);
        }
        break;
    default:
        /* nothing to do */
        break;
    }

    watchdog_indicate(rur->up_aspect, rur->replica_id, watchdog_ok);

    if (mut_synch->uplink_state == RAD_UL_INITIAL_STATE) {
        mut_synch->uplink_state = RAD_UL_QUERY_COMMON_CONFIG;
    }
    if ((mut_synch->uplink_state == RAD_UL_PRIME_READ && mut_synch->read_plan.prime_read_length == 0)
            || (mut_synch->uplink_state == RAD_UL_FLIPPED_READ && mut_synch->read_plan.flipped_read_length == 0)) {
        mut_synch->uplink_state = RAD_UL_REFILL_BUFFERS;
    }
    if (mut_synch->uplink_state == RAD_UL_REFILL_BUFFERS
            && !mut_synch->read_plan.needs_update_all && !mut_synch->read_plan.needs_alt_update) {
        mut_synch->uplink_state = RAD_UL_WRITE_TO_STREAM;
    }
    pipe_txn_t txn;
    pipe_send_prepare(&txn, rur->up_pipe, rur->replica_id);
    if (mut_synch->uplink_state == RAD_UL_WRITE_TO_STREAM) {
        uint32_t uplink_length = mut_synch->read_plan.prime_read_length + mut_synch->read_plan.flipped_read_length;
        if (uplink_length == 0) {
            mut_synch->uplink_state = RAD_UL_QUERY_STATE;
        } else if (pipe_send_allowed(&txn)) {
            assert(uplink_length <= UPLINK_BUF_LOCAL_SIZE
                     && UPLINK_BUF_LOCAL_SIZE <= pipe_message_size(rur->up_pipe));
            // write all the data we just pulled to the stream before continuing
            pipe_send_message(&txn, rur->mut->uplink_buf_local, uplink_length, 0);
            mut_synch->uplink_state = RAD_UL_QUERY_STATE;
            debugf(TRACE, "Radio uplink received %u bytes.", uplink_length);
        }
    }
    pipe_send_commit(&txn);

    switch (mut_synch->uplink_state) {
    case RAD_UL_QUERY_COMMON_CONFIG:
        // validate basic radio configuration settings
        rmap_read_start(&rmap_txn, 0x00, RADIO_REG_BASE_ADDR + REG_MAGIC * sizeof(uint32_t), sizeof(uint32_t) * 3);
        static_assert(REG_MAGIC + 1 == REG_MEM_BASE, "register layout assumptions");
        static_assert(REG_MAGIC + 2 == REG_MEM_SIZE, "register layout assumptions");
        break;
    case RAD_UL_DISABLE_RECEIVE:
        // disable receiver
        registers[0] = htobe32(RX_STATE_IDLE);
        rmap_write_start(&rmap_txn, 0x00, RADIO_REG_BASE_ADDR + REG_RX_STATE * sizeof(uint32_t),
                         (uint8_t*) registers, sizeof(uint32_t));
        break;
    case RAD_UL_RESET_REGISTERS:
        // clear remaining registers so that we have a known safe state to start from
        registers[0] = 0;
        registers[1] = 0;
        registers[2] = 0;
        registers[3] = 0;
        // no need to use htobe32... they're already zero!
        rmap_write_start(&rmap_txn, 0x00, RADIO_REG_BASE_ADDR + REG_RX_PTR * sizeof(uint32_t),
                         (uint8_t*) registers, sizeof(uint32_t) * 4);
        static_assert(REG_RX_PTR + 1 == REG_RX_LEN, "register layout assumptions");
        static_assert(REG_RX_PTR + 2 == REG_RX_PTR_ALT, "register layout assumptions");
        static_assert(REG_RX_PTR + 3 == REG_RX_LEN_ALT, "register layout assumptions");
        break;
    case RAD_UL_QUERY_STATE:
        // query reception state
        rmap_read_start(&rmap_txn, 0x00, RADIO_REG_BASE_ADDR + REG_RX_PTR * sizeof(uint32_t), sizeof(uint32_t) * 5);
        static_assert(REG_RX_PTR + 1 == REG_RX_LEN, "register layout assumptions");
        static_assert(REG_RX_PTR + 2 == REG_RX_PTR_ALT, "register layout assumptions");
        static_assert(REG_RX_PTR + 3 == REG_RX_LEN_ALT, "register layout assumptions");
        static_assert(REG_RX_PTR + 4 == REG_RX_STATE, "register layout assumptions");
        break;
    case RAD_UL_PRIME_READ:
        assert(mut_synch->read_plan.prime_read_length > 0);
        rmap_read_start(&rmap_txn, 0x00, RADIO_MEM_BASE_ADDR + mut_synch->read_plan.prime_read_address,
                                                               mut_synch->read_plan.prime_read_length);
        break;
    case RAD_UL_FLIPPED_READ:
        assert(mut_synch->read_plan.flipped_read_length > 0);
        rmap_read_start(&rmap_txn, 0x00, RADIO_MEM_BASE_ADDR + mut_synch->read_plan.flipped_read_address,
                                                               mut_synch->read_plan.flipped_read_length);
        break;
    case RAD_UL_REFILL_BUFFERS:
        assert(mut_synch->read_plan.needs_update_all || mut_synch->read_plan.needs_alt_update);
        if (mut_synch->read_plan.needs_update_all) {
            for (int i = 0; i < 5; i++) {
                registers[REG_RX_PTR + i] = htobe32(mut_synch->read_plan.new_registers[i]);
                debugf(TRACE, "Writing register %u <- 0x%08x", REG_RX_PTR + i, mut_synch->read_plan.new_registers[i]);
            }
            rmap_write_start(&rmap_txn, 0x00, RADIO_REG_BASE_ADDR + REG_RX_PTR * sizeof(uint32_t),
                             (uint8_t*) (registers + REG_RX_PTR), sizeof(uint32_t) * 5);
        } else {
            for (int i = 0; i < 2; i++) {
                registers[REG_RX_PTR_ALT + i] =
                        htobe32(mut_synch->read_plan.new_registers[i + REG_RX_PTR_ALT - REG_RX_PTR]);
                debugf(TRACE, "Writing register %u <- 0x%08x",
                       REG_RX_PTR_ALT + i, mut_synch->read_plan.new_registers[i + REG_RX_PTR_ALT - REG_RX_PTR]);
            }
            rmap_write_start(&rmap_txn, 0x00, RADIO_REG_BASE_ADDR + REG_RX_PTR_ALT * sizeof(uint32_t),
                             (uint8_t*) (registers + REG_RX_PTR_ALT), sizeof(uint32_t) * 2);
        }
        break;
    default:
        /* nothing to do */
        break;
    }

    rmap_epoch_commit(&rmap_txn);
}
