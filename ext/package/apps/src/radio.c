#include <arpa/inet.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "radio.h"

enum {
    RADIO_MAGIC    = 0x7E1ECA11,
    REG_BASE_ADDR  = 0x0000,

    // local buffer within radio.c
    UPLINK_BUF_LOCAL_SIZE = 0x1000,

    RX_STATE_IDLE      = 0x00,
    RX_STATE_LISTENING = 0x01,
    RX_STATE_OVERFLOW  = 0x02,
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
    radio->bytes_extracted = 0;

    // arbitrarily use up_ctx for this initial configuration
    if (!radio_identify(radio, &radio->up_ctx)) {
        fprintf(stderr, "Radio: could not identify device settings.\n");
        exit(1);
    }

    thread_create(&radio->up_thread, radio_uplink_loop, radio);
    thread_create(&radio->down_thread, radio_downlink_loop, radio);
}

static bool radio_read_memory(radio_t *radio, rmap_context_t *ctx, uint32_t rel_address, size_t read_len, void *read_out) {
    rmap_status_t status;
    assert(radio != NULL && ctx != NULL && read_out != NULL);
    assert(read_len <= RMAP_MAX_DATA_LEN);
    size_t actual_read = read_len;
    status = rmap_read(ctx, &radio->address, RF_INCREMENT, 0x00, rel_address + radio->mem_access_base, &actual_read, read_out);
    if (status != RS_OK) {
        fprintf(stderr, "Radio: invalid status while reading memory at 0x%x of length 0x%zx: 0x%03x\n",
                rel_address, read_len, status);
        return false;
    }
    if (actual_read != read_len) {
        fprintf(stderr, "Radio: invalid read length while reading memory at 0x%x of length 0x%zx: 0x%zx\n",
                rel_address, read_len, actual_read);
        return false;
    }
    return true;
}

static bool radio_read_registers(radio_t *radio, rmap_context_t *ctx,
                                 radio_register_t first_reg, radio_register_t last_reg, uint32_t *output) {
    rmap_status_t status;
    assert(output != NULL);
    assert(0 <= first_reg && first_reg <= last_reg && last_reg < NUM_REGISTERS);
    size_t expected_read_len = (last_reg - first_reg + 1) * 4;
    size_t actual_read_len = expected_read_len;
    // fetch the data over the network
    status = rmap_read(ctx, &radio->address, RF_INCREMENT, 0x00, first_reg, &actual_read_len, output);
    if (status != RS_OK) {
        fprintf(stderr, "Radio: invalid status while querying registers [%u, %u]: 0x%03x\n",
                first_reg, last_reg, status);
        return false;
    }
    if (actual_read_len != expected_read_len) {
        fprintf(stderr, "Radio: invalid read length while querying registers [%u, %u]: %zu instead of %zu\n",
                first_reg, last_reg, actual_read_len, expected_read_len);
        return false;
    }
    // now convert from big-endian
    for (int i = 0; i <= last_reg - first_reg; i++) {
        output[i] = ntohl(output[i]);
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
    assert(0 <= first_reg && first_reg <= last_reg && last_reg < NUM_REGISTERS);
    size_t num_regs = last_reg - first_reg + 1;
    uint32_t input_copy[num_regs];
    // convert to big-endian
    for (int i = 0; i < num_regs; i++) {
        input_copy[i] = ntohl(input[i]);
    }
    // transmit the data over the network
    status = rmap_write(ctx, &radio->address, RF_VERIFY | RF_ACKNOWLEDGE | RF_INCREMENT, 0x00, first_reg, num_regs * 4, input_copy);
    if (status != RS_OK) {
        fprintf(stderr, "Radio: invalid status while updating registers [%u, %u]: 0x%03x\n",
                first_reg, last_reg, status);
        return false;
    }
    return true;
}

static bool radio_identify(radio_t *radio, rmap_context_t *ctx) {
    uint32_t magic_num;
    if (!radio_read_register(radio, ctx, REG_MAGIC, &magic_num)) {
        return false;
    }
    if (magic_num != RADIO_MAGIC) {
        fprintf(stderr, "Radio: invalid magic number 0x%08x when 0x%08x was expected.\n", magic_num, RADIO_MAGIC);
        return false;
    }
    uint32_t mem_base, mem_size;
    if (!radio_read_register(radio, ctx, REG_MEM_BASE, &mem_base) ||
            !radio_read_register(radio, ctx, REG_MEM_SIZE, &mem_size)) {
        return false;
    }
    // alignment check is just here as a spot check... could be eliminated if radio config changed to not be aligned
    if (mem_base % 0x1000 != 0 || mem_size % 0x1000 != 0 ||
            mem_base < 0x1000 || mem_size < 0x1000 ||
            mem_base > RMAP_MAX_DATA_LEN || mem_size > RMAP_MAX_DATA_LEN) {
        fprintf(stderr, "Radio: memory range base=0x%x, size=0x%x does not satisfy constraints.\n", mem_base, mem_size);
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

// interacts with radio to read from and rotate virtual ring buffer;
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
        fprintf(stderr, "Radio: initializing uplink out of IDLE mode\n");

        radio->bytes_extracted = 0;
        reg[REG_RX_PTR] = radio->rx_halves[0].base;
        reg[REG_RX_LEN] = radio->rx_halves[0].size;
        reg[REG_RX_PTR_ALT] = radio->rx_halves[1].base;
        reg[REG_RX_LEN_ALT] = radio->rx_halves[1].size;
        reg[REG_RX_STATE] = RX_STATE_LISTENING;

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
    assert(read_length >= 0 && read_half_offset + read_length <= radio->rx_halves[read_half].size);
    assert(read_length_flip >= 0 && read_length_flip <= radio->rx_halves[read_half ? 0 : 1].size);

    // constrain the read to the actual size of the temporary buffer
    if (read_length > UPLINK_BUF_LOCAL_SIZE) {
        read_length = UPLINK_BUF_LOCAL_SIZE;
        read_length_flip = 0;
    } else if (read_length + read_length_flip > UPLINK_BUF_LOCAL_SIZE) {
        read_length_flip = UPLINK_BUF_LOCAL_SIZE - read_length;
    }

    // and perform both the prime and flipped reads as necessary
    assert(read_length >= 0 && read_length <= UPLINK_BUF_LOCAL_SIZE);
    if (!radio_read_memory(radio, &radio->up_ctx, radio->rx_halves[read_half].base + read_half_offset, read_length, radio->uplink_buf_local)) {
        return -1;
    }

    assert(read_length_flip >= 0 && read_length_flip <= UPLINK_BUF_LOCAL_SIZE - read_length);
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
            fprintf(stderr, "Radio: uplink OVERFLOW condition hit; clearing and resuming uplink\n");
            if (!radio_write_registers(radio, &radio->up_ctx, REG_RX_PTR, REG_RX_STATE, reg + REG_RX_PTR)) {
                return -1;
            }
        } else if (end_index_alt == 0) {
            // we need to refill the alternate pointer and length
            assert(reg[REG_RX_STATE] == RX_STATE_LISTENING);
            reg[REG_RX_PTR_ALT] = new_region.base;
            reg[REG_RX_LEN_ALT] = new_region.size;
            if (!radio_write_registers(radio, &radio->up_ctx, REG_RX_PTR_ALT, REG_RX_LEN_ALT, reg + REG_RX_PTR)) {
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
            fprintf(stderr, "Radio: hit error in uplink loop; halting uplink thread.\n");
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
            usleep(1000);
        }
    }
}

static void *radio_downlink_loop(void *radio_opaque) {
    // TODO
    fprintf(stderr, "Downlink: UNIMPLEMENTED\n");
    return NULL;
}