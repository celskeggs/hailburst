#include <assert.h>
#include <endian.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "debug.h"
#include "radio.h"

// #define DEBUGIDX

enum {
    RADIO_MAGIC    = 0x7E1ECA11,
    REG_BASE_ADDR  = 0x0000,

    // local buffer within radio.c
    UPLINK_BUF_LOCAL_SIZE   = 0x1000,
    DOWNLINK_BUF_LOCAL_SIZE = 0x1000,

    RX_STATE_IDLE      = 0x00,
    RX_STATE_LISTENING = 0x01,
    RX_STATE_OVERFLOW  = 0x02,

    TX_STATE_IDLE   = 0x00,
    TX_STATE_ACTIVE = 0x01,

    TRANSACTION_RETRIES = 5,
};

enum {
    RADIO_RS_PACKET_CORRUPTED   = 0x01,
    RADIO_RS_REGISTER_READ_ONLY = 0x02,
    RADIO_RS_INVALID_ADDRESS    = 0x03,
    RADIO_RS_VALUE_OUT_OF_RANGE = 0x04,
};

typedef enum {
    REG_MAGIC      = 0,
    REG_TX_PTR     = 1,
    REG_TX_LEN     = 2,
    REG_TX_STATE   = 3,
    REG_RX_PTR     = 4,
    REG_RX_LEN     = 5,
    REG_RX_PTR_ALT = 6,
    REG_RX_LEN_ALT = 7,
    REG_RX_STATE   = 8,
    REG_ERR_COUNT  = 9,
    REG_MEM_BASE   = 10,
    REG_MEM_SIZE   = 11,
    NUM_REGISTERS  = 12,
} radio_register_t;

static bool radio_identify(radio_t *radio, rmap_context_t *ctx);
static void *radio_uplink_loop(void *radio_opaque);
static void *radio_downlink_loop(void *radio_opaque);

void radio_init(radio_t *radio, rmap_monitor_t *mon, rmap_addr_t *address, ringbuf_t *uplink, ringbuf_t *downlink) {
    assert(radio != NULL && mon != NULL && address != NULL && uplink != NULL && downlink != NULL);
    assert(ringbuf_elem_size(uplink) == 1);
    assert(ringbuf_elem_size(downlink) == 1);
    size_t max_write_len = ringbuf_capacity(downlink);
    if (max_write_len > RMAP_MAX_DATA_LEN) {
        max_write_len = RMAP_MAX_DATA_LEN;
    }
    rmap_init_context(&radio->down_ctx, mon, max_write_len);
    rmap_init_context(&radio->up_ctx, mon, NUM_REGISTERS * sizeof(uint32_t));
    memcpy(&radio->address, address, sizeof(rmap_addr_t));
    radio->up_ring = uplink;
    radio->down_ring = downlink;
    radio->uplink_buf_local = malloc(UPLINK_BUF_LOCAL_SIZE);
    assert(radio->uplink_buf_local != NULL);
    radio->downlink_buf_local = malloc(DOWNLINK_BUF_LOCAL_SIZE);
    assert(radio->downlink_buf_local != NULL);
    radio->bytes_extracted = 0;

    // arbitrarily use up_ctx for this initial configuration
    if (!radio_identify(radio, &radio->up_ctx)) {
        debug0("Radio: could not identify device settings.");
        exit(1);
    }

    thread_create(&radio->up_thread, "radio_up_loop", radio_uplink_loop, radio);
    thread_create(&radio->down_thread, "radio_down_loop", radio_downlink_loop, radio);
}

static bool radio_is_error_recoverable(rmap_status_t status) {
    assert(status != RS_OK);
    switch ((uint32_t) status) {
    // indicates failure of lower network stack; no point in retrying.
    case RS_EXCHANGE_DOWN:
        return false;
    case RS_RECVLOOP_STOPPED:
        return false;
    // indicates likely packet corruption; worth retrying in case it works again.
    case RS_DATA_TRUNCATED:
        return true;
    case RS_TRANSACTION_TIMEOUT:
        return true;
    case RADIO_RS_PACKET_CORRUPTED:
        return true;
    // indicates programming error or program code corruption; not worth retrying. we want these to be surfaced.
    case RADIO_RS_REGISTER_READ_ONLY:
        return false;
    case RADIO_RS_INVALID_ADDRESS:
        return false;
    case RADIO_RS_VALUE_OUT_OF_RANGE:
        return false;
    // if not known, assume we can't recover.
    default:
        return false;
    }
}

static bool radio_read_memory(radio_t *radio, rmap_context_t *ctx, uint32_t rel_address, size_t read_len, void *read_out) {
    rmap_status_t status;
    assert(radio != NULL && ctx != NULL && read_out != NULL);
    assert(0 < read_len && read_len <= RMAP_MAX_DATA_LEN);
    size_t actual_read;
    int retries = TRANSACTION_RETRIES;

retry:
    actual_read = read_len;
    status = rmap_read(ctx, &radio->address, RF_INCREMENT, 0x00, rel_address + radio->mem_access_base, &actual_read, read_out);

    if (status != RS_OK) {
        if (!radio_is_error_recoverable(status)) {
            debugf("Radio: encountered unrecoverable error while reading memory at 0x%x of length 0x%zx: 0x%03x",
                   rel_address, read_len, status);
            return false;
        } else if (retries > 0) {
            debugf("Radio: retrying memory read at 0x%x of length 0x%zx after recoverable error: 0x%03x",
                   rel_address, read_len, status);
            goto retry;
        } else {
            debugf("Radio: after %d retries, erroring out during memory read at 0x%x of length 0x%zx: 0x%03x",
                   TRANSACTION_RETRIES, rel_address, read_len, status);
            return false;
        }
    }
    if (actual_read != read_len) {
        debugf("Radio: invalid read length while reading memory at 0x%x of length 0x%zx: 0x%zx",
               rel_address, read_len, actual_read);
        return false;
    }
    return true;
}

static bool radio_write_memory(radio_t *radio, rmap_context_t *ctx, uint32_t rel_address, size_t write_len, void *write_in) {
    rmap_status_t status;
    assert(radio != NULL && ctx != NULL && write_in != NULL);
    assert(0 < write_len && write_len <= RMAP_MAX_DATA_LEN);
    int retries = TRANSACTION_RETRIES;

retry:
    status = rmap_write(ctx, &radio->address, RF_VERIFY | RF_ACKNOWLEDGE | RF_INCREMENT,
                        0x00, rel_address + radio->mem_access_base, write_len, write_in);

    if (status != RS_OK) {
        if (!radio_is_error_recoverable(status)) {
            debugf("Radio: encountered unrecoverable error while writing memory at 0x%x of length 0x%zx: 0x%03x",
                   rel_address, write_len, status);
            return false;
        } else if (retries > 0) {
            debugf("Radio: retrying memory write at 0x%x of length 0x%zx after recoverable error: 0x%03x",
                   rel_address, write_len, status);
            goto retry;
        } else {
            debugf("Radio: after %d retries, erroring out during memory write at 0x%x of length 0x%zx: 0x%03x",
                   TRANSACTION_RETRIES, rel_address, write_len, status);
            return false;
        }
    }
    return true;
}

static bool radio_read_registers(radio_t *radio, rmap_context_t *ctx,
                                 radio_register_t first_reg, radio_register_t last_reg, uint32_t *output) {
    rmap_status_t status;
    assert(output != NULL);
    assert(first_reg <= last_reg && last_reg < NUM_REGISTERS);
    size_t expected_read_len = (last_reg - first_reg + 1) * 4;
    assert(expected_read_len > 0);
    size_t actual_read_len;
    int retries = TRANSACTION_RETRIES;

retry:
    // fetch the data over the network
    actual_read_len = expected_read_len;
    status = rmap_read(ctx, &radio->address, RF_INCREMENT, 0x00, first_reg * 4, &actual_read_len, output);
    if (status != RS_OK) {
        if (!radio_is_error_recoverable(status)) {
            debugf("Radio: encountered unrecoverable error while querying registers [%u, %u]: 0x%03x",
                   first_reg, last_reg, status);
            return false;
        } else if (retries > 0) {
            debugf("Radio: retrying register query [%u, %u] after recoverable error: 0x%03x",
                   first_reg, last_reg, status);
            goto retry;
        } else {
            debugf("Radio: after %d retries, erroring out during register query [%u, %u]: 0x%03x",
                   TRANSACTION_RETRIES, first_reg, last_reg, status);
            return false;
        }
    }
    if (actual_read_len != expected_read_len) {
        debugf("Radio: invalid read length while querying registers [%u, %u]: %zu instead of %zu",
               first_reg, last_reg, actual_read_len, expected_read_len);
        return false;
    }
    // now convert from big-endian
    for (int i = 0; i <= last_reg - first_reg; i++) {
        output[i] = be32toh(output[i]);
    }
    return true;
}

static bool radio_read_register(radio_t *radio, rmap_context_t *ctx, radio_register_t reg, uint32_t *output) {
    return radio_read_registers(radio, ctx, reg, reg, output);
}

static bool radio_write_registers(radio_t *radio, rmap_context_t *ctx,
                                  radio_register_t first_reg, radio_register_t last_reg, uint32_t *input) {
    rmap_status_t status;
    assert(input != NULL);
    assert(first_reg <= last_reg && last_reg < NUM_REGISTERS);
    size_t num_regs = last_reg - first_reg + 1;
    uint32_t input_copy[num_regs];
    // convert to big-endian
    for (size_t i = 0; i < num_regs; i++) {
        input_copy[i] = be32toh(input[i]);
    }
    assert(num_regs > 0);
    int retries = TRANSACTION_RETRIES;

retry:
    // transmit the data over the network
    status = rmap_write(ctx, &radio->address, RF_VERIFY | RF_ACKNOWLEDGE | RF_INCREMENT, 0x00, first_reg * 4, num_regs * 4, input_copy);
    if (status != RS_OK) {
        if (!radio_is_error_recoverable(status)) {
            debugf("Radio: encountered unrecoverable error while updating registers [%u, %u]: 0x%03x",
                   first_reg, last_reg, status);
            return false;
        } else if (retries > 0) {
            debugf("Radio: retrying register update [%u, %u] after recoverable error: 0x%03x",
                   first_reg, last_reg, status);
            goto retry;
        } else {
            debugf("Radio: after %d retries, erroring out during register update [%u, %u]: 0x%03x",
                   TRANSACTION_RETRIES, first_reg, last_reg, status);
            return false;
        }
    }
    return true;
}

static bool radio_identify(radio_t *radio, rmap_context_t *ctx) {
    uint32_t magic_num;
    if (!radio_read_register(radio, ctx, REG_MAGIC, &magic_num)) {
        return false;
    }
    if (magic_num != RADIO_MAGIC) {
        debugf("Radio: invalid magic number 0x%08x when 0x%08x was expected.", magic_num, RADIO_MAGIC);
        return false;
    }
    uint32_t mem_base, mem_size;
    if (!radio_read_register(radio, ctx, REG_MEM_BASE, &mem_base) ||
            !radio_read_register(radio, ctx, REG_MEM_SIZE, &mem_size)) {
        return false;
    }
    // alignment check is just here as a spot check... could be eliminated if radio config changed to not be aligned
    if (mem_base % 0x100 != 0 || mem_size % 0x100 != 0 ||
            mem_base < 0x100 || mem_size < 0x100 ||
            mem_base > RMAP_MAX_DATA_LEN || mem_size > RMAP_MAX_DATA_LEN) {
        debugf("Radio: memory range base=0x%x, size=0x%x does not satisfy constraints.", mem_base, mem_size);
        return false;
    }
    radio->mem_access_base = mem_base;

    radio->rx_halves[0].base = 0;
    radio->rx_halves[1].base = mem_size / 4;
    radio->tx_region.base = mem_size / 2;

    radio->rx_halves[0].size = mem_size / 4;
    radio->rx_halves[1].size = mem_size / 4;
    radio->tx_region.size = mem_size / 2;

    return true;
}

/*************************************************************************************************
 * The big challenge with radio reception is that we need to be able to CONTINUOUSLY receive     *
 * data from the ground, even if we're currently transferring part of the buffer to the FSW.     *
 * In order to support this, the radio implementation provides a pair of RX buffer pointers and  *
 * lengths; implementing a ring buffer would be difficult, but we can have a active/passive      *
 * buffering arrangement without too much trouble.                                               *
 *************************************************************************************************/

// interacts with radio to read from and flip virtual ping-pong buffer;
// returns number of bytes placed into the uplink_buf_local buffer, or negative numbers on error.
static ssize_t radio_uplink_service(radio_t *radio) {
    _Static_assert(REG_RX_PTR + 1 == REG_RX_LEN, "register layout assumptions");
    _Static_assert(REG_RX_PTR + 2 == REG_RX_PTR_ALT, "register layout assumptions");
    _Static_assert(REG_RX_PTR + 3 == REG_RX_LEN_ALT, "register layout assumptions");
    _Static_assert(REG_RX_PTR + 4 == REG_RX_STATE, "register layout assumptions");
    uint32_t reg[NUM_REGISTERS];
    if (!radio_read_registers(radio, &radio->up_ctx, REG_RX_PTR, REG_RX_STATE, reg + REG_RX_PTR)) {
        return -1;
    }

    if (reg[REG_RX_STATE] == RX_STATE_IDLE) {
        debug0("Radio: initializing uplink out of IDLE mode");

        radio->bytes_extracted = 0;
        reg[REG_RX_PTR] = radio->rx_halves[0].base;
        reg[REG_RX_LEN] = radio->rx_halves[0].size;
        reg[REG_RX_PTR_ALT] = radio->rx_halves[1].base;
        reg[REG_RX_LEN_ALT] = radio->rx_halves[1].size;
        reg[REG_RX_STATE] = RX_STATE_LISTENING;

#ifdef DEBUGIDX
        debugf("Radio UPDATED indices: end_index_prime=%u, end_index_alt=%u",
               reg[REG_RX_PTR] + reg[REG_RX_LEN], reg[REG_RX_PTR_ALT] + reg[REG_RX_LEN_ALT]);
#endif

        if (!radio_write_registers(radio, &radio->up_ctx, REG_RX_PTR, REG_RX_STATE, reg + REG_RX_PTR)) {
            return -1;
        }
        // no data to read, because we just initialized the buffers
        return 0;
    }
    // otherwise, we've already been initialized, and can go look to read back previous results.

    // start by identifying what the current positions mean.
    uint32_t end_index_h0 = radio->rx_halves[0].base + radio->rx_halves[0].size;
    uint32_t end_index_h1 = radio->rx_halves[1].base + radio->rx_halves[1].size;

    uint32_t end_index_prime = reg[REG_RX_PTR] + reg[REG_RX_LEN];
    uint32_t end_index_alt = reg[REG_RX_PTR_ALT] + reg[REG_RX_LEN_ALT];
#ifdef DEBUGIDX
    debugf("Radio indices: end_index_h0=%u, end_index_h1=%u, end_index_prime=%u, end_index_alt=%u, extracted=%u",
           end_index_h0, end_index_h1, end_index_prime, end_index_alt, radio->bytes_extracted);
#endif
    assert(end_index_prime == end_index_h0
        || end_index_prime == end_index_h1);
    assert(end_index_prime != end_index_alt);
    if (end_index_alt == 0) {
        assert(reg[REG_RX_PTR_ALT] == 0 && reg[REG_RX_LEN_ALT] == 0);
    } else {
        assert(end_index_alt == end_index_h0
            || end_index_alt == end_index_h1);
    }

    // identify where the next read location should be...
    uint32_t read_cycle_offset = radio->bytes_extracted % (radio->rx_halves[0].size + radio->rx_halves[1].size);
    int read_half = (read_cycle_offset >= radio->rx_halves[0].size) ? 1 : 0;
    uint32_t read_half_offset = read_cycle_offset - (read_half ? radio->rx_halves[0].size : 0);

    uint32_t read_length; // bytes to read from current read half
    uint32_t read_length_flip; // bytes to read from opposite read half

    if (end_index_alt == 0) {
        // then we WERE in the non-prime half, and switched, which means the read index MUST be in the non-prime half
        if (end_index_prime == end_index_h0) {
            assert(read_half == 1);
        } else /* end_index_prime == end_index_h1 */ {
            assert(read_half == 0);
        }
        read_length = radio->rx_halves[read_half].size - read_half_offset;
        read_length_flip = reg[REG_RX_PTR] - radio->rx_halves[read_half ? 0 : 1].base;
    } else {
        // then we ARE in the prime half, and the read index must be here
        if (end_index_prime == end_index_h0) {
            assert(read_half == 0);
        } else /* end_index_prime == end_index_h1 */ {
            assert(read_half == 1);
        }
        read_length = (reg[REG_RX_PTR] - radio->rx_halves[read_half].base) - read_half_offset;
        read_length_flip = 0;
    }
    assert(read_half_offset + read_length <= radio->rx_halves[read_half].size);
    assert(read_length_flip <= radio->rx_halves[read_half ? 0 : 1].size);

    // constrain the read to the actual size of the temporary buffer
    if (read_length > UPLINK_BUF_LOCAL_SIZE) {
        read_length = UPLINK_BUF_LOCAL_SIZE;
        read_length_flip = 0;
    } else if (read_length + read_length_flip > UPLINK_BUF_LOCAL_SIZE) {
        read_length_flip = UPLINK_BUF_LOCAL_SIZE - read_length;
    }

    // and perform both the prime and flipped reads as necessary
    assert(read_length <= UPLINK_BUF_LOCAL_SIZE);
    if (read_length > 0) {
        if (!radio_read_memory(radio, &radio->up_ctx, radio->rx_halves[read_half].base + read_half_offset, read_length, radio->uplink_buf_local)) {
            return -1;
        }
    }

    assert(read_length_flip <= UPLINK_BUF_LOCAL_SIZE - read_length);
    if (read_length_flip > 0) {
        if (!radio_read_memory(radio, &radio->up_ctx, radio->rx_halves[read_half ? 0 : 1].base, read_length_flip, radio->uplink_buf_local + read_length)) {
            return -1;
        }
    }

    uint32_t total_read = read_length + read_length_flip;
    radio->bytes_extracted += total_read;

    // now that we've read a chunk of data, we need to consider whether we'll be updating the pointers.

    // quick coherency check: if we are in OVERFLOW condition, then we must have run out of data on our prime buffer.
    if (reg[REG_RX_STATE] == RX_STATE_OVERFLOW) {
        assert(reg[REG_RX_LEN] == 0);
    }

    // new question: is there any unread data in the alternate half?
    uint32_t reread_cycle_offset = radio->bytes_extracted % (radio->rx_halves[0].size + radio->rx_halves[1].size);
    int reread_half = (reread_cycle_offset >= radio->rx_halves[0].size) ? 1 : 0;

    bool any_unread_data_in_alternate = (reread_half == 0 && end_index_prime == end_index_h1)
                                     || (reread_half == 1 && end_index_prime == end_index_h0);

#ifdef DEBUGIDX
    debugf("Unread stats: bytes_extracted=%u, reread_half=%d, a_u_d_i_a=%d, ptr=%u, ptr_alt=%u",
           radio->bytes_extracted, reread_half, any_unread_data_in_alternate, reg[REG_RX_PTR], reg[REG_RX_PTR_ALT]);
#endif

    if (any_unread_data_in_alternate) {
        // then we CANNOT safely have the alternate pointer and length set! we will have to finish reading.
        assert(end_index_alt == 0);
    } else {
        // then we CAN safely refill the alternate pointer and length.
        memregion_t new_region = (end_index_prime == end_index_h1) ? radio->rx_halves[0] : radio->rx_halves[1];
        if (reg[REG_RX_STATE] == RX_STATE_OVERFLOW) {
            // simulate effect of flip
            reg[REG_RX_PTR] = new_region.base;
            reg[REG_RX_LEN] = new_region.size;
            reg[REG_RX_PTR_ALT] = 0;
            reg[REG_RX_LEN_ALT] = 0;
            reg[REG_RX_STATE] = RX_STATE_LISTENING;
            debug0("Radio: uplink OVERFLOW condition hit; clearing and resuming uplink.");
#ifdef DEBUGIDX
            debugf("Radio UPDATED indices: end_index_prime=%u, end_index_alt=%u",
                   reg[REG_RX_PTR] + reg[REG_RX_LEN], reg[REG_RX_PTR_ALT] + reg[REG_RX_LEN_ALT]);
#endif
            if (!radio_write_registers(radio, &radio->up_ctx, REG_RX_PTR, REG_RX_STATE, reg + REG_RX_PTR)) {
                return -1;
            }
        } else if (end_index_alt == 0) {
            // we need to refill the alternate pointer and length
            assert(reg[REG_RX_STATE] == RX_STATE_LISTENING);
            reg[REG_RX_PTR_ALT] = new_region.base;
            reg[REG_RX_LEN_ALT] = new_region.size;
#ifdef DEBUGIDX
            debugf("Radio UPDATED indices: end_index_prime=<unchanged>, end_index_alt=%u",
                   reg[REG_RX_PTR_ALT] + reg[REG_RX_LEN_ALT]);
#endif
            if (!radio_write_registers(radio, &radio->up_ctx, REG_RX_PTR_ALT, REG_RX_LEN_ALT, reg + REG_RX_PTR_ALT)) {
                return -1;
            }
        } else {
            // or, in this case, no refill is actually necessary!
        }
    }

    return total_read;
}

static void *radio_uplink_loop(void *radio_opaque) {
    radio_t *radio = (radio_t *) radio_opaque;
    assert(radio != NULL);
    for (;;) {
        ssize_t grabbed = radio_uplink_service(radio);
        if (grabbed < 0) {
            debug0("Radio: hit error in uplink loop; halting uplink thread.");
            return NULL;
        } else if (grabbed > 0) {
            assert(grabbed <= UPLINK_BUF_LOCAL_SIZE);
            // write all the data we just pulled to the ring buffer before continuing
            ringbuf_write_all(radio->up_ring, radio->uplink_buf_local, grabbed);

            // NOTE: if there's not enough space in the ring buffer, and we block, and the radio ends up overflowing
            // the buffer... then that's a problem with us not reading the ring buffer fast enough, not a problem with
            // us blocking on writing to the ring buffer.
        }

        // only sleep if we haven't been reading all that much data. if we have, then we'd better keep at it!
        if (grabbed < 500) {
            usleep(10000);
        }
    }
}

static bool radio_downlink_service(radio_t *radio, size_t append_len) {
    uint32_t state;
    // make sure the radio is idle
    if (!radio_read_register(radio, &radio->down_ctx, REG_TX_STATE, &state)) {
        return false;
    }
    assert(state == TX_STATE_IDLE);

    // write the new transmission into radio memory
    if (!radio_write_memory(radio, &radio->down_ctx, radio->tx_region.base, append_len, radio->downlink_buf_local)) {
        return false;
    }

    // start the write
    _Static_assert(REG_TX_PTR + 1 == REG_TX_LEN, "register layout assumptions");
    _Static_assert(REG_TX_PTR + 2 == REG_TX_STATE, "register layout assumptions");
    assert(append_len <= radio->tx_region.size);
    uint32_t reg[] = {
        /* REG_TX_PTR */   radio->tx_region.base,
        /* REG_TX_LEN */   append_len,
        /* REG_TX_STATE */ TX_STATE_ACTIVE,
    };
    if (!radio_write_registers(radio, &radio->down_ctx, REG_TX_PTR, REG_TX_STATE, reg)) {
        return false;
    }

    // monitor the write until it completes
    uint32_t cur_len = 0;
    for (;;) {
        if (!radio_read_register(radio, &radio->down_ctx, REG_TX_LEN, &cur_len)) {
            return false;
        }
        if (cur_len > 0) {
            usleep(cur_len + 5);
        } else {
            break;
        }
    }

    // confirm that the radio has, in fact, stopped transmitting
    if (!radio_read_register(radio, &radio->down_ctx, REG_TX_STATE, &state)) {
        return false;
    }
    assert(state == TX_STATE_IDLE);

    debugf("Radio: finished transmitting %zu bytes.", append_len);

    return true;
}

static void *radio_downlink_loop(void *radio_opaque) {
    radio_t *radio = (radio_t *) radio_opaque;
    assert(radio != NULL);
    size_t max_len = radio->tx_region.size;
    if (max_len > DOWNLINK_BUF_LOCAL_SIZE) {
        max_len = DOWNLINK_BUF_LOCAL_SIZE;
    }
    assert(max_len > 0);
    for (;;) {
        size_t grabbed = ringbuf_read(radio->down_ring, radio->downlink_buf_local, max_len, RB_BLOCKING);
        assert(grabbed > 0 && grabbed <= DOWNLINK_BUF_LOCAL_SIZE && grabbed <= radio->tx_region.size);

        if (!radio_downlink_service(radio, grabbed)) {
            debug0("Radio: hit error in downlink loop; halting downlink thread.");
            return NULL;
        }
    }
}
