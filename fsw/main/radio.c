#include <endian.h>
#include <stdlib.h>
#include <string.h>

#include <hal/watchdog.h>
#include <fsw/debug.h>
#include <fsw/radio.h>
#include <fsw/retry.h>

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

    TRANSACTION_RETRIES = 5,
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

enum {
    RADIO_RS_PACKET_CORRUPTED   = 0x01,
    RADIO_RS_REGISTER_READ_ONLY = 0x02,
    RADIO_RS_INVALID_ADDRESS    = 0x03,
    RADIO_RS_VALUE_OUT_OF_RANGE = 0x04,
};

typedef enum {
    IO_UPLINK_CONTEXT,
    IO_DOWNLINK_CONTEXT,
} radio_io_mode_t;

static rmap_t *radio_rmap(radio_t *radio, radio_io_mode_t mode) {
    if (mode == IO_DOWNLINK_CONTEXT) {
        assert(radio->rmap_down != NULL);
        return radio->rmap_down;
    } else if (mode == IO_UPLINK_CONTEXT) {
        assert(radio->rmap_up != NULL);
        return radio->rmap_up;
    } else {
        abortf("invalid mode %u", mode);
    }
}

static rmap_addr_t *radio_routing(radio_t *radio, radio_io_mode_t mode) {
    assert(mode == IO_DOWNLINK_CONTEXT || mode == IO_UPLINK_CONTEXT);
    return mode == IO_DOWNLINK_CONTEXT ? &radio->address_down : &radio->address_up;
}

static uint8_t *radio_read_memory_fetch(radio_t *radio, radio_io_mode_t mode, uint32_t mem_address, size_t length) {
    rmap_status_t status;
    assert(radio != NULL);
    size_t actual_read;
    uint8_t *ptr_out;

    RETRY(TRANSACTION_RETRIES, "radio memory read at 0x%x of length 0x%zx, error=0x%03x", mem_address, length, status) {
        actual_read = length;
        ptr_out = NULL;
        status = rmap_read_fetch(radio_rmap(radio, mode), radio_routing(radio, mode), RF_INCREMENT, 0x00,
                                 mem_address + MEM_BASE_ADDR, &actual_read, &ptr_out);
        if (status == RS_OK) {
            assert(actual_read == length && ptr_out != NULL);
            return ptr_out;
        }
    }
    return NULL;
}

static uint8_t *radio_write_memory_prepare(radio_t *radio, radio_io_mode_t mode, uint32_t mem_address,
                                           rmap_status_t *status_out) {
    assert(radio != NULL && status_out != NULL);
    uint8_t *ptr_out = NULL;
    rmap_status_t status;
    status = rmap_write_prepare(radio_rmap(radio, mode), radio_routing(radio, mode),
                                RF_VERIFY | RF_ACKNOWLEDGE | RF_INCREMENT,
                                0x00, mem_address + MEM_BASE_ADDR, &ptr_out);
    *status_out = status;
    if (status == RS_OK) {
        assert(ptr_out != NULL);
        return ptr_out;
    }
    return NULL;
}

static bool radio_write_memory_commit(radio_t *radio, radio_io_mode_t mode, size_t write_len,
                                      rmap_status_t *status_out) {
    assert(radio != NULL && status_out != NULL);
    rmap_status_t status = rmap_write_commit(radio_rmap(radio, mode), write_len, NULL);
    *status_out = status;
    return status == RS_OK;
}

static bool radio_read_registers(radio_t *radio, radio_io_mode_t mode,
                                 radio_register_t first_reg, radio_register_t last_reg, uint32_t *output) {
    rmap_status_t status;
    assert(output != NULL);
    assert(first_reg <= last_reg && last_reg < NUM_REGISTERS);
    size_t read_len = (last_reg - first_reg + 1) * 4;
    assert(read_len > 0);

    RETRY(TRANSACTION_RETRIES, "register query on [%u, %u], error=0x%03x", first_reg, last_reg, status) {
        status = rmap_read_exact(radio_rmap(radio, mode), radio_routing(radio, mode), RF_INCREMENT, 0x00,
                                 first_reg * 4, read_len, (uint8_t *) output);
        if (status == RS_OK) {
            // convert from big-endian
            for (int i = 0; i <= (int) last_reg - (int) first_reg; i++) {
                output[i] = be32toh(output[i]);
            }
            return true;
        }
    }
    return false;
}

static bool radio_read_register(radio_t *radio, radio_io_mode_t mode, radio_register_t reg, uint32_t *output) {
    return radio_read_registers(radio, mode, reg, reg, output);
}

static bool radio_write_registers(radio_t *radio, radio_io_mode_t mode,
                                  radio_register_t first_reg, radio_register_t last_reg, uint32_t *input) {
    rmap_status_t status;
    assert(input != NULL);
    assert(first_reg <= last_reg && last_reg < NUM_REGISTERS);
    size_t num_regs = last_reg - first_reg + 1;
    assert(num_regs > 0);

    RETRY(TRANSACTION_RETRIES, "register update on [%u, %u], error=0x%03x", first_reg, last_reg, status) {
        // transmit the data over the network
        uint8_t *write_ptr = NULL;
        status = rmap_write_prepare(radio_rmap(radio, mode), radio_routing(radio, mode),
                                    RF_VERIFY | RF_ACKNOWLEDGE | RF_INCREMENT,
                                    0x00, first_reg * sizeof(uint32_t), &write_ptr);
        if (status == RS_OK) {
            assert(write_ptr != NULL);
            // convert to big-endian
            uint32_t *write_ptr32 = (uint32_t *) write_ptr;
            for (size_t i = 0; i < num_regs; i++) {
                write_ptr32[i] = be32toh(input[i]);
            }
            status = rmap_write_commit(radio_rmap(radio, mode), num_regs * sizeof(uint32_t), NULL);
            if (status == RS_OK) {
                return true;
            }
        }
    }
    return false;
}

static bool radio_write_register(radio_t *radio, radio_io_mode_t mode, radio_register_t reg, uint32_t input) {
    return radio_write_registers(radio, mode, reg, reg, &input);
}

static bool radio_initialize_common(radio_t *radio, radio_io_mode_t mode) {
    uint32_t config_data[3];
    static_assert(REG_MAGIC + 1 == REG_MEM_BASE, "register layout assumptions");
    static_assert(REG_MEM_BASE + 1 == REG_MEM_SIZE, "register layout assumptions");
    if (!radio_read_registers(radio, mode, REG_MAGIC, REG_MEM_SIZE, config_data)) {
        return false;
    }
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

static bool radio_initialize_downlink(radio_t *radio) {
    if (!radio_initialize_common(radio, IO_DOWNLINK_CONTEXT)) {
        return false;
    }

    // disable transmission and zero pointer and length registers
    uint32_t zeroes[] = { 0, 0, TX_STATE_IDLE };
    static_assert(REG_TX_PTR + 1 == REG_TX_LEN, "register layout assumptions");
    static_assert(REG_TX_LEN + 1 == REG_TX_STATE, "register layout assumptions");
    if (!radio_write_registers(radio, IO_DOWNLINK_CONTEXT, REG_TX_PTR, REG_TX_STATE, zeroes)) {
        return false;
    }

    return true;
}

static bool radio_initialize_uplink(radio_t *radio) {
    if (!radio_initialize_common(radio, IO_UPLINK_CONTEXT)) {
        return false;
    }

    // disable transmission and reception
    if (!radio_write_register(radio, IO_UPLINK_CONTEXT, REG_RX_STATE, RX_STATE_IDLE)) {
        return false;
    }

    // clear remaining registers so that we have a known safe state to start from
    uint32_t zeroes[4] = { 0, 0, 0, 0 };
    static_assert(REG_RX_PTR + 1 == REG_RX_LEN, "register layout assumptions");
    static_assert(REG_RX_PTR + 2 == REG_RX_PTR_ALT, "register layout assumptions");
    static_assert(REG_RX_PTR + 3 == REG_RX_LEN_ALT, "register layout assumptions");
    if (!radio_write_registers(radio, IO_UPLINK_CONTEXT, REG_RX_PTR, REG_RX_LEN_ALT, zeroes)) {
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

// interacts with radio to read from and flip virtual ping-pong buffer;
// returns number of bytes placed into the uplink_buf_local buffer, or negative numbers on error.
static ssize_t radio_uplink_service(radio_t *radio) {
    static_assert(REG_RX_PTR + 1 == REG_RX_LEN, "register layout assumptions");
    static_assert(REG_RX_PTR + 2 == REG_RX_PTR_ALT, "register layout assumptions");
    static_assert(REG_RX_PTR + 3 == REG_RX_LEN_ALT, "register layout assumptions");
    static_assert(REG_RX_PTR + 4 == REG_RX_STATE, "register layout assumptions");
    uint32_t reg[NUM_REGISTERS];
    if (!radio_read_registers(radio, IO_UPLINK_CONTEXT, REG_RX_PTR, REG_RX_STATE, reg + REG_RX_PTR)) {
        return -1;
    }

    if (reg[REG_RX_STATE] == RX_STATE_IDLE) {
        debugf(INFO, "Radio: initializing uplink out of IDLE mode");

        radio->bytes_extracted = 0;
        reg[REG_RX_PTR] = rx_halves[0].base;
        reg[REG_RX_LEN] = rx_halves[0].size;
        reg[REG_RX_PTR_ALT] = rx_halves[1].base;
        reg[REG_RX_LEN_ALT] = rx_halves[1].size;
        reg[REG_RX_STATE] = RX_STATE_LISTENING;

#ifdef DEBUGIDX
        debugf(TRACE, "Radio UPDATED indices: end_index_prime=%u, end_index_alt=%u",
               reg[REG_RX_PTR] + reg[REG_RX_LEN], reg[REG_RX_PTR_ALT] + reg[REG_RX_LEN_ALT]);
#endif

        if (!radio_write_registers(radio, IO_UPLINK_CONTEXT, REG_RX_PTR, REG_RX_STATE, reg + REG_RX_PTR)) {
            return -1;
        }
        // no data to read, because we just initialized the buffers
        return 0;
    }
    // otherwise, we've already been initialized, and can go look to read back previous results.

    // start by identifying what the current positions mean.
    uint32_t end_index_h0 = rx_halves[0].base + rx_halves[0].size;
    uint32_t end_index_h1 = rx_halves[1].base + rx_halves[1].size;

    uint32_t end_index_prime = reg[REG_RX_PTR] + reg[REG_RX_LEN];
    uint32_t end_index_alt = reg[REG_RX_PTR_ALT] + reg[REG_RX_LEN_ALT];
#ifdef DEBUGIDX
    debugf(TRACE, "Radio indices: end_index_h0=%u, end_index_h1=%u, end_index_prime=%u, end_index_alt=%u, extracted=%u",
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

    // and perform both the prime and flipped reads as necessary
    assert(read_length <= UPLINK_BUF_LOCAL_SIZE);
    if (read_length > 0) {
        uint8_t *src = radio_read_memory_fetch(radio, IO_UPLINK_CONTEXT,
                                               rx_halves[read_half].base + read_half_offset, read_length);
        if (src == NULL) {
            return -1;
        }
        memcpy(radio->uplink_buf_local, src, read_length);
    }

    assert(read_length_flip <= UPLINK_BUF_LOCAL_SIZE - read_length);
    if (read_length_flip > 0) {
        uint8_t *src = radio_read_memory_fetch(radio, IO_UPLINK_CONTEXT,
                                               rx_halves[read_half ? 0 : 1].base, read_length_flip);
        if (src == NULL) {
            return -1;
        }
        memcpy(radio->uplink_buf_local + read_length, src, read_length_flip);
    }

    uint32_t total_read = read_length + read_length_flip;
    radio->bytes_extracted += total_read;

    // now that we've read a chunk of data, we need to consider whether we'll be updating the pointers.

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
            if (!radio_write_registers(radio, IO_UPLINK_CONTEXT, REG_RX_PTR, REG_RX_STATE, reg + REG_RX_PTR)) {
                return -1;
            }
        } else if (end_index_alt == 0) {
            // we need to refill the alternate pointer and length
            assert(reg[REG_RX_STATE] == RX_STATE_LISTENING);
            reg[REG_RX_PTR_ALT] = new_region.base;
            reg[REG_RX_LEN_ALT] = new_region.size;
#ifdef DEBUGIDX
            debugf(TRACE, "Radio UPDATED indices: end_index_prime=<unchanged>, end_index_alt=%u",
                   reg[REG_RX_PTR_ALT] + reg[REG_RX_LEN_ALT]);
#endif
            if (!radio_write_registers(radio, IO_UPLINK_CONTEXT,
                                       REG_RX_PTR_ALT, REG_RX_LEN_ALT, reg + REG_RX_PTR_ALT)) {
                return -1;
            }
        } else {
            // or, in this case, no refill is actually necessary!
        }
    }

    return total_read;
}

void radio_uplink_loop(radio_t *radio) {
    assert(radio != NULL);

    // (re)configure uplink side of radio
    if (!radio_initialize_uplink(radio)) {
        debugf(WARNING, "Radio: could not identify device settings for uplink.");
        return;
    }

    for (;;) {
        ssize_t grabbed = radio_uplink_service(radio);
        if (grabbed < 0) {
            debugf(WARNING, "Radio: hit error in uplink loop; halting uplink thread.");
            break;
        } else if (grabbed > 0) {
            assert(grabbed <= UPLINK_BUF_LOCAL_SIZE);
            // write all the data we just pulled to the stream before continuing
            stream_write(radio->up_stream, radio->uplink_buf_local, grabbed);

            // NOTE: if there's not enough space in the stream, and we block, and the radio ends up overflowing its
            // buffer... then that's a problem with us not reading the stream fast enough, not a problem with us
            // blocking on writing to the stream.
        }

        // only sleep if we haven't been reading all that much data. if we have, then we'd better keep at it!
        if (grabbed < 500) {
            task_delay(10000000);
        }

        watchdog_ok(WATCHDOG_ASPECT_RADIO_UPLINK);
    }
}

static bool radio_downlink_service(radio_t *radio, size_t append_len) {
    uint32_t state;
    // make sure the radio is idle
    if (!radio_read_register(radio, IO_DOWNLINK_CONTEXT, REG_TX_STATE, &state)) {
        return false;
    }
    assert(state == TX_STATE_IDLE);

    // TODO: eliminate need for separate local downlink buffer

    // write the new transmission into radio memory
    rmap_status_t status = RS_INVALID_ERR;
    RETRY(TRANSACTION_RETRIES, "radio memory write at 0x%x of length 0x%zx, error=0x%03x",
                               tx_region.base, append_len, status) {
        uint8_t *write_target = radio_write_memory_prepare(radio, IO_DOWNLINK_CONTEXT, tx_region.base, &status);
        if (write_target == NULL) {
            continue;
        }
        memcpy(write_target, radio->downlink_buf_local, append_len);
        if (!radio_write_memory_commit(radio, IO_DOWNLINK_CONTEXT, append_len, &status)) {
            continue;
        }
        break;
    }
    if (status != RS_OK) {
        return false;
    }

    // start the write
    static_assert(REG_TX_PTR + 1 == REG_TX_LEN, "register layout assumptions");
    static_assert(REG_TX_PTR + 2 == REG_TX_STATE, "register layout assumptions");
    assert(append_len <= tx_region.size);
    uint32_t reg[] = {
        /* REG_TX_PTR */   tx_region.base,
        /* REG_TX_LEN */   append_len,
        /* REG_TX_STATE */ TX_STATE_ACTIVE,
    };
    if (!radio_write_registers(radio, IO_DOWNLINK_CONTEXT, REG_TX_PTR, REG_TX_STATE, reg)) {
        return false;
    }

    // monitor the write until it completes
    uint32_t cur_len = 0;
    for (;;) {
        if (!radio_read_register(radio, IO_DOWNLINK_CONTEXT, REG_TX_LEN, &cur_len)) {
            return false;
        }
        if (cur_len > 0) {
            task_delay((cur_len + 5) * 1000);
        } else {
            break;
        }
    }

    // confirm that the radio has, in fact, stopped transmitting
    if (!radio_read_register(radio, IO_DOWNLINK_CONTEXT, REG_TX_STATE, &state)) {
        return false;
    }
    assert(state == TX_STATE_IDLE);

    return true;
}

void radio_downlink_loop(radio_t *radio) {
    assert(radio != NULL);

    // (re)configure downlink side of radio
    if (!radio_initialize_downlink(radio)) {
        debugf(WARNING, "Radio: could not identify device settings for downlink.");
        return;
    }

    size_t max_len = tx_region.size;
    if (max_len > DOWNLINK_BUF_LOCAL_SIZE) {
        max_len = DOWNLINK_BUF_LOCAL_SIZE;
    }
    assert(max_len > 0);
    for (;;) {
        size_t grabbed = stream_read(radio->down_stream, radio->downlink_buf_local, max_len);
        assert(grabbed > 0 && grabbed <= DOWNLINK_BUF_LOCAL_SIZE && grabbed <= tx_region.size);

        debugf(TRACE, "Radio downlink received %zu bytes for transmission.", grabbed);
        if (!radio_downlink_service(radio, grabbed)) {
            debugf(WARNING, "Radio: hit error in downlink loop; halting downlink thread.");
            break;
        }
        debugf(TRACE, "Radio downlink completed transmitting %zu bytes.", grabbed);

        watchdog_ok(WATCHDOG_ASPECT_RADIO_DOWNLINK);
    }
}
