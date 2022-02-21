#include <endian.h>
#include <stdlib.h>
#include <string.h>

#include <hal/debug.h>
#include <hal/watchdog.h>
#include <synch/retry.h>
#include <flight/radio.h>

// #define DEBUGIDX

enum {
    RADIO_MAGIC    = 0x7E1ECA11,
    REG_BASE_ADDR  = 0x0000,
    MEM_BASE_ADDR  = 0x1000,
    MEM_SIZE       = 0x4000,

    RX_STATE_IDLE      = 0x00,
    RX_STATE_LISTENING = 0x01,
    RX_STATE_OVERFLOW  = 0x02,

    TX_STATE_IDLE   = 0x00,
    TX_STATE_ACTIVE = 0x01,
};

typedef struct {
    uint32_t base;
    uint32_t size;
} memregion_t;

const memregion_t rx_halves[] = {
    [0] = { .base = 0,            .size = MEM_SIZE / 4 },
    [1] = { .base = MEM_SIZE / 4, .size = MEM_SIZE / 4 },
};
const memregion_t tx_region = { .base = MEM_SIZE / 2, .size = MEM_SIZE / 2 };

static bool radio_validate_common_config(uint32_t config_data[3]) {
    if (config_data[0] != RADIO_MAGIC) {
        debugf(CRITICAL, "Invalid magic number 0x%08x when 0x%08x was expected.", config_data[0], RADIO_MAGIC);
        return false;
    }
    if (config_data[1] != MEM_BASE_ADDR) {
        debugf(CRITICAL, "Invalid base address 0x%08x when 0x%08x was expected.", config_data[1], MEM_BASE_ADDR);
        return false;
    }
    if (config_data[2] != MEM_SIZE) {
        debugf(CRITICAL, "Invalid memory size 0x%08x when 0x%08x was expected.", config_data[2], MEM_SIZE);
        return false;
    }
    return true;
}

/*************************************************************************************************
 * The big challenge with radio reception is that we need to be able to CONTINUOUSLY receive     *
 * data from the ground, even if we're currently transferring part of the buffer to the FSW.     *
 * In order to support this, the radio implementation provides a pair of RX buffer pointers and  *
 * lengths; implementing a ring buffer would be difficult, but we can have a active/passive      *
 * buffering arrangement without too much trouble.                                               *
 *************************************************************************************************/

struct uplink_reads {
    uint32_t prime_read_address;
    uint32_t prime_read_length;
    uint32_t flipped_read_address;
    uint32_t flipped_read_length;
    bool needs_update_all; // if set, then register array has new values for all five core registers written back
    bool needs_alt_update; // if set, then register array has new values for PTR_ALT and LEN_ALT only
};

static void radio_uplink_compute_reads(radio_t *radio, uint32_t reg[NUM_REGISTERS], struct uplink_reads *reads) {
    assert(radio != NULL && reg != NULL && reads != NULL);

    if (reg[REG_RX_STATE] == RX_STATE_IDLE) {
        debugf(INFO, "Radio: initializing uplink out of IDLE mode");

        radio->bytes_extracted = 0;
        reg[REG_RX_PTR] = rx_halves[0].base;
        reg[REG_RX_LEN] = rx_halves[0].size;
        reg[REG_RX_PTR_ALT] = rx_halves[1].base;
        reg[REG_RX_LEN_ALT] = rx_halves[1].size;
        reg[REG_RX_STATE] = RX_STATE_LISTENING;

        // no data to read; just initialize the buffers
        *reads = (struct uplink_reads) {
            .prime_read_length = 0,
            .flipped_read_length = 0,
            .needs_update_all = true,
        };

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
           end_index_h0, end_index_h1, end_index_prime, end_index_alt, radio->bytes_extracted);
#endif
    assert(end_index_prime == end_index_h0 || end_index_prime == end_index_h1);
    assert(end_index_prime != end_index_alt);
    if (end_index_alt == 0) {
        assert(reg[REG_RX_PTR_ALT] == 0 && reg[REG_RX_LEN_ALT] == 0);
    } else {
        assert(end_index_alt == end_index_h0 || end_index_alt == end_index_h1);
    }

    // identify where the next read location should be...
    uint32_t read_cycle_offset = radio->bytes_extracted % (rx_halves[0].size + rx_halves[1].size);
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

    *reads = (struct uplink_reads) {
        .prime_read_address = rx_halves[read_half].base + read_half_offset,
        .prime_read_length = read_length,
        .flipped_read_address = rx_halves[read_half ? 0 : 1].base,
        .flipped_read_length = read_length_flip,
        .needs_update_all = false, /* updated later if necessary */
        .needs_alt_update = false, /* updated later if necessary */
    };

    uint32_t total_read = read_length + read_length_flip;
    radio->bytes_extracted += total_read;

    // quick coherency check: if we are in OVERFLOW condition, then we must have run out of data on our prime buffer.
    if (reg[REG_RX_STATE] == RX_STATE_OVERFLOW) {
        assert(reg[REG_RX_LEN] == 0);
    }

    // new question: is there any unread data in the alternate half?
    uint32_t reread_cycle_offset = radio->bytes_extracted % (rx_halves[0].size + rx_halves[1].size);
    int reread_half = (reread_cycle_offset >= rx_halves[0].size) ? 1 : 0;

    bool any_unread_data_in_alternate = (reread_half == 0 && end_index_prime == end_index_h1)
                                     || (reread_half == 1 && end_index_prime == end_index_h0);

#ifdef DEBUGIDX
    debugf(TRACE, "Unread stats: bytes_extracted=%u, reread_half=%d, a_u_d_i_a=%d, ptr=%u, ptr_alt=%u",
           radio->bytes_extracted, reread_half, any_unread_data_in_alternate, reg[REG_RX_PTR], reg[REG_RX_PTR_ALT]);
#endif

    if (any_unread_data_in_alternate) {
        // then we CANNOT safely have the alternate pointer and length set! we will have to finish reading.
        assert(end_index_alt == 0);
    } else {
        // then we CAN safely refill the alternate pointer and length.
        memregion_t new_region = (end_index_prime == end_index_h1) ? rx_halves[0] : rx_halves[1];
        if (reg[REG_RX_STATE] == RX_STATE_OVERFLOW) {
            // simulate effect of flip
            reg[REG_RX_PTR] = new_region.base;
            reg[REG_RX_LEN] = new_region.size;
            reg[REG_RX_PTR_ALT] = 0;
            reg[REG_RX_LEN_ALT] = 0;
            reg[REG_RX_STATE] = RX_STATE_LISTENING;
            debugf(CRITICAL, "Radio: uplink OVERFLOW condition hit; clearing and resuming uplink.");

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
#ifdef DEBUGIDX
            debugf(TRACE, "Radio UPDATED indices: end_index_prime=<unchanged>, end_index_alt=%u",
                   reg[REG_RX_PTR_ALT] + reg[REG_RX_LEN_ALT]);
#endif
            reads->needs_alt_update = true;
        } else {
            // or, in this case, no refill is actually necessary!
        }
    }

    // run this here, so that it only happens AFTER we initialize out of idle mode.
    watchdog_ok(WATCHDOG_ASPECT_RADIO_UPLINK);
}

enum uplink_state {
    RAD_UL_INITIAL_STATE,
    RAD_UL_QUERY_COMMON_CONFIG,
    RAD_UL_DISABLE_RECEIVE,
    RAD_UL_RESET_REGISTERS,
    RAD_UL_QUERY_STATE,
    RAD_UL_PRIME_READ,
    RAD_UL_FLIPPED_READ,
    RAD_UL_REFILL_BUFFERS,
    RAD_UL_WRITE_TO_STREAM,
};

void radio_uplink_loop(radio_t *radio) {
    assert(radio != NULL);

    enum uplink_state state = RAD_UL_INITIAL_STATE;

    rmap_status_t status;
    size_t uplink_offset = 0;
    uint32_t registers[NUM_REGISTERS];
    struct uplink_reads read_plan = { };

    for (;;) {
        rmap_epoch_prepare(radio->rmap_up);

        switch (state) {
        case RAD_UL_QUERY_COMMON_CONFIG:
            status = rmap_read_complete(radio->rmap_up, (uint8_t*) registers, sizeof(uint32_t) * 3, NULL);
            if (status == RS_OK) {
                for (int i = 0; i < 3; i++) {
                    registers[i] = be32toh(registers[i]);
                }
                if (!radio_validate_common_config(registers)) {
                    // invalid radio; stop.
                    return;
                }
                state = RAD_UL_DISABLE_RECEIVE;
            } else {
                debugf(WARNING, "Failed to read initial radio metadata, error=0x%03x", status);
            }
            break;
        case RAD_UL_DISABLE_RECEIVE:
            status = rmap_write_complete(radio->rmap_up, NULL);
            if (status == RS_OK) {
                state = RAD_UL_RESET_REGISTERS;
            } else {
                debugf(WARNING, "Failed to disable radio receiver, error=0x%03x", status);
            }
            break;
        case RAD_UL_RESET_REGISTERS:
            status = rmap_write_complete(radio->rmap_up, NULL);
            if (status == RS_OK) {
                state = RAD_UL_QUERY_STATE;
            } else {
                debugf(WARNING, "Failed to reset radio receiver to known state, error=0x%03x", status);
            }
            break;
        case RAD_UL_QUERY_STATE:
            status = rmap_read_complete(radio->rmap_up,
                                        (uint8_t*) (registers + REG_RX_PTR), sizeof(uint32_t) * 5, NULL);
            if (status == RS_OK) {
                for (int i = REG_RX_PTR; i < REG_RX_PTR + 5; i++) {
                    registers[i] = be32toh(registers[i]);
                }
                radio_uplink_compute_reads(radio, registers, &read_plan);
                uplink_offset = 0;
                state = RAD_UL_PRIME_READ;
            } else {
                debugf(WARNING, "Failed to query radio status, error=0x%03x", status);
            }
            break;
        case RAD_UL_PRIME_READ:
            status = rmap_read_complete(radio->rmap_up, radio->uplink_buf_local, read_plan.prime_read_length, NULL);
            if (status == RS_OK) {
                state = RAD_UL_FLIPPED_READ;
            } else {
                debugf(WARNING, "Failed to read prime memory region, error=0x%03x", status);
            }
            break;
        case RAD_UL_FLIPPED_READ:
            status = rmap_read_complete(radio->rmap_up, radio->uplink_buf_local + read_plan.prime_read_length,
                                                        read_plan.flipped_read_length, NULL);
            if (status == RS_OK) {
                state = RAD_UL_REFILL_BUFFERS;
            } else {
                debugf(WARNING, "Failed to read flipped memory region, error=0x%03x", status);
            }
            break;
        case RAD_UL_REFILL_BUFFERS:
            status = rmap_write_complete(radio->rmap_up, NULL);
            if (status == RS_OK) {
                state = RAD_UL_WRITE_TO_STREAM;
            } else {
                debugf(WARNING, "Failed to refill receiver buffers, error=0x%03x", status);
            }
            break;
        default:
            /* nothing to do */
            break;
        }

        if (state == RAD_UL_INITIAL_STATE) {
            state = RAD_UL_QUERY_COMMON_CONFIG;
        }
        if ((state == RAD_UL_PRIME_READ && read_plan.prime_read_length == 0)
                || (state == RAD_UL_FLIPPED_READ && read_plan.flipped_read_length == 0)) {
            state = RAD_UL_REFILL_BUFFERS;
        }
        if (state == RAD_UL_REFILL_BUFFERS && !read_plan.needs_update_all && !read_plan.needs_alt_update) {
            state = RAD_UL_WRITE_TO_STREAM;
        }
        if (state == RAD_UL_WRITE_TO_STREAM) {
            uint32_t uplink_length = read_plan.prime_read_length + read_plan.flipped_read_length;
            if (uplink_length == 0) {
                state = RAD_UL_QUERY_STATE;
            } else {
                assert(uplink_offset < uplink_length && uplink_length <= UPLINK_BUF_LOCAL_SIZE);
                // write all the data we just pulled to the stream before continuing
                uplink_offset += stream_write_nonblock(radio->up_stream, radio->uplink_buf_local + uplink_offset,
                                                                         uplink_length - uplink_offset);
                if (uplink_offset == uplink_length) {
                    state = RAD_UL_QUERY_STATE;
                    debugf(TRACE, "Radio uplink received %u bytes.", uplink_length);
                }
                assert(uplink_offset <= uplink_length);
            }
        }

        switch (state) {
        case RAD_UL_QUERY_COMMON_CONFIG:
            // validate basic radio configuration settings
            rmap_read_start(radio->rmap_up, 0x00, REG_BASE_ADDR + REG_MAGIC * sizeof(uint32_t), sizeof(uint32_t) * 3);
            static_assert(REG_MAGIC + 1 == REG_MEM_BASE, "register layout assumptions");
            static_assert(REG_MAGIC + 2 == REG_MEM_SIZE, "register layout assumptions");
            break;
        case RAD_UL_DISABLE_RECEIVE:
            // disable receiver
            registers[0] = htobe32(RX_STATE_IDLE);
            rmap_write_start(radio->rmap_up, 0x00, REG_BASE_ADDR + REG_RX_STATE * sizeof(uint32_t),
                             (uint8_t*) registers, sizeof(uint32_t));
            break;
        case RAD_UL_RESET_REGISTERS:
            // clear remaining registers so that we have a known safe state to start from
            registers[0] = 0;
            registers[1] = 0;
            registers[2] = 0;
            registers[3] = 0;
            // no need to use htobe32... they're already zero!
            rmap_write_start(radio->rmap_up, 0x00, REG_BASE_ADDR + REG_RX_PTR * sizeof(uint32_t),
                             (uint8_t*) registers, sizeof(uint32_t) * 4);
            static_assert(REG_RX_PTR + 1 == REG_RX_LEN, "register layout assumptions");
            static_assert(REG_RX_PTR + 2 == REG_RX_PTR_ALT, "register layout assumptions");
            static_assert(REG_RX_PTR + 3 == REG_RX_LEN_ALT, "register layout assumptions");
            break;
        case RAD_UL_QUERY_STATE:
            // query reception state
            rmap_read_start(radio->rmap_up, 0x00, REG_BASE_ADDR + REG_RX_PTR * sizeof(uint32_t), sizeof(uint32_t) * 5);
            static_assert(REG_RX_PTR + 1 == REG_RX_LEN, "register layout assumptions");
            static_assert(REG_RX_PTR + 2 == REG_RX_PTR_ALT, "register layout assumptions");
            static_assert(REG_RX_PTR + 3 == REG_RX_LEN_ALT, "register layout assumptions");
            static_assert(REG_RX_PTR + 4 == REG_RX_STATE, "register layout assumptions");
            break;
        case RAD_UL_PRIME_READ:
            assert(read_plan.prime_read_length > 0);
            rmap_read_start(radio->rmap_up,
                            0x00, MEM_BASE_ADDR + read_plan.prime_read_address, read_plan.prime_read_length);
            break;
        case RAD_UL_FLIPPED_READ:
            assert(read_plan.flipped_read_length > 0);
            rmap_read_start(radio->rmap_up,
                            0x00, MEM_BASE_ADDR + read_plan.flipped_read_address, read_plan.flipped_read_length);
            break;
        case RAD_UL_REFILL_BUFFERS:
            assert(read_plan.needs_update_all || read_plan.needs_alt_update);
            if (read_plan.needs_update_all) {
                for (int i = REG_RX_PTR; i < REG_RX_PTR + 5; i++) {
                    registers[i] = htobe32(registers[i]);
                }
                rmap_write_start(radio->rmap_up, 0x00, REG_BASE_ADDR + REG_RX_PTR * sizeof(uint32_t),
                                 (uint8_t*) (registers + REG_RX_PTR), sizeof(uint32_t) * 5);
            } else {
                for (int i = REG_RX_PTR_ALT; i < REG_RX_PTR_ALT + 2; i++) {
                    registers[i] = htobe32(registers[i]);
                }
                rmap_write_start(radio->rmap_up, 0x00, REG_BASE_ADDR + REG_RX_PTR_ALT * sizeof(uint32_t),
                                 (uint8_t*) (registers + REG_RX_PTR_ALT), sizeof(uint32_t) * 2);
            }
            break;
        default:
            /* nothing to do */
            break;
        }

        rmap_epoch_commit(radio->rmap_up);

        task_yield();
    }
}

enum downlink_state {
    RAD_DL_INITIAL_STATE,
    RAD_DL_QUERY_COMMON_CONFIG,
    RAD_DL_DISABLE_TRANSMIT,
    RAD_DL_WAITING_FOR_STREAM,
    RAD_DL_VALIDATE_IDLE,
    RAD_DL_WRITE_RADIO_MEMORY,
    RAD_DL_START_TRANSMIT,
    RAD_DL_MONITOR_TRANSMIT,
    RAD_DL_VERIFY_COMPLETE,
};

void radio_downlink_loop(radio_t *radio) {
    assert(radio != NULL);

    enum downlink_state state = RAD_DL_INITIAL_STATE;

    // scratch variables for use in switch statements
    rmap_status_t status;
    uint32_t registers[3];
    uint32_t downlink_length = 0;

    for (;;) {
        rmap_epoch_prepare(radio->rmap_down);

        switch (state) {
        case RAD_DL_QUERY_COMMON_CONFIG:
            status = rmap_read_complete(radio->rmap_down, (uint8_t*) registers, sizeof(uint32_t) * 3, NULL);
            if (status == RS_OK) {
                for (int i = 0; i < 3; i++) {
                    registers[i] = be32toh(registers[i]);
                }
                if (!radio_validate_common_config(registers)) {
                    // invalid radio; stop.
                    return;
                }
                state = RAD_DL_DISABLE_TRANSMIT;
            } else {
                debugf(WARNING, "Failed to read initial radio metadata, error=0x%03x", status);
            }
            break;
        case RAD_DL_DISABLE_TRANSMIT:
            status = rmap_write_complete(radio->rmap_down, NULL);
            if (status == RS_OK) {
                state = RAD_DL_WAITING_FOR_STREAM;
            } else {
                debugf(WARNING, "Failed to disable radio transmitter, error=0x%03x", status);
            }
            break;
        case RAD_DL_VALIDATE_IDLE:
            status = rmap_read_complete(radio->rmap_down, (uint8_t*) registers, sizeof(uint32_t), NULL);
            if (status == RS_OK) {
                registers[0] = be32toh(registers[0]);
                if (registers[0] != TX_STATE_IDLE) {
                    debugf(WARNING, "Radio transmitter is unexpectedly not IDLE (%u).", registers[0]);
                    return;
                }
                state = RAD_DL_WRITE_RADIO_MEMORY;
            } else {
                debugf(WARNING, "Failed to query radio transmit state, error=0x%03x", status);
            }
            break;
        case RAD_DL_WRITE_RADIO_MEMORY:
            status = rmap_write_complete(radio->rmap_down, NULL);
            if (status == RS_OK) {
                state = RAD_DL_START_TRANSMIT;
            } else {
                debugf(WARNING, "Failed to write transmission to radio memory, error=0x%03x", status);
            }
            break;
        case RAD_DL_START_TRANSMIT:
            status = rmap_write_complete(radio->rmap_down, NULL);
            if (status == RS_OK) {
                state = RAD_DL_MONITOR_TRANSMIT;
            } else {
                debugf(WARNING, "Failed to start radio transmission, error=0x%03x", status);
            }
            break;
        case RAD_DL_MONITOR_TRANSMIT:
            status = rmap_read_complete(radio->rmap_down, (uint8_t*) registers, sizeof(uint32_t), NULL);
            if (status == RS_OK) {
                registers[0] = be32toh(registers[0]);
                if (registers[0] == 0) {
                    state = RAD_DL_VERIFY_COMPLETE;
                } else {
                    debugf(TRACE, "Remaining bytes to transmit: %u/%u.", registers[0], downlink_length);
                }
            } else {
                debugf(WARNING, "Failed to query radio transmit bytes remaining, error=0x%03x", status);
            }
            break;
        case RAD_DL_VERIFY_COMPLETE:
            status = rmap_read_complete(radio->rmap_down, (uint8_t*) registers, sizeof(uint32_t), NULL);
            if (status == RS_OK) {
                registers[0] = be32toh(registers[0]);
                assert(downlink_length >= 1 && downlink_length <= DOWNLINK_BUF_LOCAL_SIZE);
                if (registers[0] != TX_STATE_IDLE) {
                    debugf(WARNING, "Radio has not yet reached IDLE (%u).", registers[0]);
                } else {
                    state = RAD_DL_WAITING_FOR_STREAM;
                    debugf(TRACE, "Radio downlink completed transmitting %u bytes.", downlink_length);
                    downlink_length = 0;
                    watchdog_ok(WATCHDOG_ASPECT_RADIO_DOWNLINK);
                }
            } else {
                debugf(WARNING, "Failed to query radio transmit status, error=0x%03x", status);
            }
            break;
        default:
            /* nothing to do */
            break;
        }

        if (state == RAD_DL_INITIAL_STATE) {
            state = RAD_DL_QUERY_COMMON_CONFIG;
        } else if (state == RAD_DL_WAITING_FOR_STREAM) {
            assert(tx_region.size >= DOWNLINK_BUF_LOCAL_SIZE);
            downlink_length = stream_read(radio->down_stream, radio->downlink_buf_local,
                                          DOWNLINK_BUF_LOCAL_SIZE, false);
            if (downlink_length > 0) {
                assert(downlink_length <= DOWNLINK_BUF_LOCAL_SIZE);
                state = RAD_DL_VALIDATE_IDLE;
                debugf(TRACE, "Radio downlink received %u bytes for transmission.", downlink_length);
            }
        }

        switch (state) {
        case RAD_DL_QUERY_COMMON_CONFIG:
            // validate basic radio configuration settings
            rmap_read_start(radio->rmap_down, 0x00,
                            REG_BASE_ADDR + REG_MAGIC * sizeof(uint32_t), sizeof(uint32_t) * 3);
            static_assert(REG_MAGIC + 1 == REG_MEM_BASE, "register layout assumptions");
            static_assert(REG_MAGIC + 2 == REG_MEM_SIZE, "register layout assumptions");
            break;
        case RAD_DL_DISABLE_TRANSMIT:
            // disable transmission and zero pointer and length registers
            registers[0] = 0;
            registers[1] = 0;
            registers[2] = htobe32(TX_STATE_IDLE);
            rmap_write_start(radio->rmap_down, 0x00, REG_TX_PTR * sizeof(uint32_t),
                             (uint8_t*) registers, sizeof(uint32_t) * 3);
            static_assert(REG_TX_PTR + 1 == REG_TX_LEN, "register layout assumptions");
            static_assert(REG_TX_PTR + 2 == REG_TX_STATE, "register layout assumptions");
            break;
        case RAD_DL_VALIDATE_IDLE:
            // validate that radio is idle
            rmap_read_start(radio->rmap_down, 0x00, REG_BASE_ADDR + REG_TX_STATE * sizeof(uint32_t), sizeof(uint32_t));
            break;
        case RAD_DL_WRITE_RADIO_MEMORY:
            // place data into radio memory
            assert(downlink_length >= 1 && downlink_length <= DOWNLINK_BUF_LOCAL_SIZE);
            rmap_write_start(radio->rmap_down, 0x00, MEM_BASE_ADDR + tx_region.base,
                             radio->downlink_buf_local, downlink_length);
            break;
        case RAD_DL_START_TRANSMIT:
            // enable transmission
            assert(downlink_length >= 1 && downlink_length <= DOWNLINK_BUF_LOCAL_SIZE
                                        && downlink_length <= tx_region.size);
            registers[0] = htobe32(tx_region.base);
            registers[1] = htobe32(downlink_length);
            registers[2] = htobe32(TX_STATE_ACTIVE);
            rmap_write_start(radio->rmap_down, 0x00, REG_BASE_ADDR + REG_TX_PTR * sizeof(uint32_t),
                             (uint8_t*) registers, sizeof(uint32_t) * 3);
            static_assert(REG_TX_PTR + 1 == REG_TX_LEN, "register layout assumptions");
            static_assert(REG_TX_PTR + 2 == REG_TX_STATE, "register layout assumptions");
            break;
        case RAD_DL_MONITOR_TRANSMIT:
            // check how many bytes remain to be transmitted
            rmap_read_start(radio->rmap_down, 0x00, REG_BASE_ADDR + REG_TX_LEN * sizeof(uint32_t), sizeof(uint32_t));
            break;
        case RAD_DL_VERIFY_COMPLETE:
            // validate that radio is idle
            rmap_read_start(radio->rmap_down, 0x00, REG_BASE_ADDR + REG_TX_STATE * sizeof(uint32_t), sizeof(uint32_t));
            break;
        default:
            /* nothing to do */
            break;
        }

        rmap_epoch_commit(radio->rmap_down);

        task_yield();
    }
}
