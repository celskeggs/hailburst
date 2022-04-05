#include <endian.h>
#include <stdlib.h>
#include <string.h>

#include <hal/debug.h>
#include <flight/radio.h>

enum {
    TX_STATE_IDLE   = 0x00,
    TX_STATE_ACTIVE = 0x01,
};

const radio_memregion_t tx_region = { .base = RADIO_MEM_SIZE / 2, .size = RADIO_MEM_SIZE / 2 };

void radio_downlink_clip(radio_downlink_replica_t *rdr) {
    assert(rdr != NULL && rdr->mut != NULL);

    // scratch variables for use in switch statements
    rmap_status_t status;
    uint32_t registers[NUM_REGISTERS];

    if (clip_is_restart()) {
        rdr->mut->downlink_state = RAD_DL_INITIAL_STATE;
    }

    rmap_txn_t rmap_txn;
    rmap_epoch_prepare(&rmap_txn, rdr->rmap_down);

    bool watchdog_ok = false;

    switch (rdr->mut->downlink_state) {
    case RAD_DL_QUERY_COMMON_CONFIG:
        status = rmap_read_complete(&rmap_txn, (uint8_t*) registers, sizeof(uint32_t) * 3, NULL);
        if (status == RS_OK) {
            for (int i = 0; i < 3; i++) {
                registers[i] = be32toh(registers[i]);
            }
            if (!radio_validate_common_config(registers)) {
                // invalid radio; stop.
                return;
            }
            rdr->mut->downlink_state = RAD_DL_DISABLE_TRANSMIT;
        } else {
            debugf(WARNING, "Failed to read initial radio metadata, error=0x%03x", status);
        }
        break;
    case RAD_DL_DISABLE_TRANSMIT:
        status = rmap_write_complete(&rmap_txn, NULL);
        if (status == RS_OK) {
            rdr->mut->downlink_state = RAD_DL_WAITING_FOR_STREAM;
        } else {
            debugf(WARNING, "Failed to disable radio transmitter, error=0x%03x", status);
        }
        break;
    case RAD_DL_WRITE_RADIO_MEMORY:
        status = rmap_write_complete(&rmap_txn, NULL);
        if (status == RS_OK) {
            rdr->mut->downlink_state = RAD_DL_START_TRANSMIT;
        } else {
            debugf(WARNING, "Failed to write transmission to radio memory, error=0x%03x", status);
        }
        break;
    case RAD_DL_START_TRANSMIT:
        status = rmap_write_complete(&rmap_txn, NULL);
        if (status == RS_OK) {
            rdr->mut->downlink_state = RAD_DL_MONITOR_TRANSMIT;
        } else {
            debugf(WARNING, "Failed to start radio transmission, error=0x%03x", status);
        }
        break;
    case RAD_DL_MONITOR_TRANSMIT:
        assert(rdr->mut->downlink_length >= 1 && rdr->mut->downlink_length <= DOWNLINK_BUF_LOCAL_SIZE);
        status = rmap_read_complete(&rmap_txn, (uint8_t*) registers, sizeof(uint32_t) * 2, NULL);
        if (status == RS_OK) {
            registers[0] = be32toh(registers[0]);
            registers[1] = be32toh(registers[1]);
            if (registers[0] == 0) {
                if (registers[1] != TX_STATE_IDLE) {
                    debugf(WARNING, "Radio has not yet reached IDLE (%u).", registers[1]);
                } else {
                    rdr->mut->downlink_state = RAD_DL_WAITING_FOR_STREAM;
                    debugf(TRACE, "Radio downlink completed transmitting %u bytes.", rdr->mut->downlink_length);
                    rdr->mut->downlink_length = 0;
                    watchdog_ok = true;
                }
            } else {
                debugf(TRACE, "Remaining bytes to transmit: %u/%u.", registers[0], rdr->mut->downlink_length);
            }
        } else {
            debugf(WARNING, "Failed to query radio transmit status, error=0x%03x", status);
        }
        break;
    default:
        /* nothing to do */
        break;
    }

    watchdog_indicate(rdr->down_aspect, rdr->replica_id, watchdog_ok);

    if (rdr->mut->downlink_state == RAD_DL_INITIAL_STATE) {
        rdr->mut->downlink_state = RAD_DL_QUERY_COMMON_CONFIG;
    }
    pipe_txn_t txn;
    pipe_receive_prepare(&txn, rdr->down_pipe, rdr->replica_id);
    if (rdr->mut->downlink_state == RAD_DL_WAITING_FOR_STREAM) {
        assert(tx_region.size >= DOWNLINK_BUF_LOCAL_SIZE);
        assert(pipe_message_size(rdr->down_pipe) <= DOWNLINK_BUF_LOCAL_SIZE);
        rdr->mut->downlink_length = pipe_receive_message(&txn, rdr->mut->downlink_buf_local, NULL);
        if (rdr->mut->downlink_length > 0) {
            assert(rdr->mut->downlink_length <= DOWNLINK_BUF_LOCAL_SIZE);
            rdr->mut->downlink_state = RAD_DL_WRITE_RADIO_MEMORY;
            debugf(TRACE, "Radio downlink received %u bytes for transmission.", rdr->mut->downlink_length);
        }
    }
    // we can only start requesting data once we know we can accept it.
    // (maybe there's a way to streamline this?)
    pipe_receive_commit(&txn, (rdr->mut->downlink_state == RAD_DL_WAITING_FOR_STREAM) ? 1 : 0);

    switch (rdr->mut->downlink_state) {
    case RAD_DL_QUERY_COMMON_CONFIG:
        // validate basic radio configuration settings
        rmap_read_start(&rmap_txn, 0x00, RADIO_REG_BASE_ADDR + REG_MAGIC * sizeof(uint32_t), sizeof(uint32_t) * 3);
        static_assert(REG_MAGIC + 1 == REG_MEM_BASE, "register layout assumptions");
        static_assert(REG_MAGIC + 2 == REG_MEM_SIZE, "register layout assumptions");
        break;
    case RAD_DL_DISABLE_TRANSMIT:
        // disable transmission and zero pointer and length registers
        registers[0] = 0;
        registers[1] = 0;
        registers[2] = htobe32(TX_STATE_IDLE);
        rmap_write_start(&rmap_txn, 0x00, REG_TX_PTR * sizeof(uint32_t), (uint8_t*) registers, sizeof(uint32_t) * 3);
        static_assert(REG_TX_PTR + 1 == REG_TX_LEN, "register layout assumptions");
        static_assert(REG_TX_PTR + 2 == REG_TX_STATE, "register layout assumptions");
        break;
    case RAD_DL_WRITE_RADIO_MEMORY:
        // place data into radio memory
        assert(rdr->mut->downlink_length >= 1 && rdr->mut->downlink_length <= DOWNLINK_BUF_LOCAL_SIZE);
        rmap_write_start(&rmap_txn, 0x00, RADIO_MEM_BASE_ADDR + tx_region.base,
                         rdr->mut->downlink_buf_local, rdr->mut->downlink_length);
        break;
    case RAD_DL_START_TRANSMIT:
        // enable transmission
        assert(rdr->mut->downlink_length >= 1 && rdr->mut->downlink_length <= DOWNLINK_BUF_LOCAL_SIZE
                                              && rdr->mut->downlink_length <= tx_region.size);
        registers[0] = htobe32(tx_region.base);
        registers[1] = htobe32(rdr->mut->downlink_length);
        registers[2] = htobe32(TX_STATE_ACTIVE);
        rmap_write_start(&rmap_txn, 0x00, RADIO_REG_BASE_ADDR + REG_TX_PTR * sizeof(uint32_t),
                         (uint8_t*) registers, sizeof(uint32_t) * 3);
        static_assert(REG_TX_PTR + 1 == REG_TX_LEN, "register layout assumptions");
        static_assert(REG_TX_PTR + 2 == REG_TX_STATE, "register layout assumptions");
        break;
    case RAD_DL_MONITOR_TRANSMIT:
        // check how many bytes remain to be transmitted, and validate that the radio is idle
        rmap_read_start(&rmap_txn, 0x00, RADIO_REG_BASE_ADDR + REG_TX_LEN * sizeof(uint32_t), sizeof(uint32_t) * 2);
        static_assert(REG_TX_LEN + 1 == REG_TX_STATE, "register layout assumptions");
        break;
    default:
        /* nothing to do */
        break;
    }

    rmap_epoch_commit(&rmap_txn);
}
