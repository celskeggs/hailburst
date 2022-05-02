#include <stdlib.h>
#include <string.h>

#include <hal/atomic.h>
#include <hal/debug.h>
#include <hal/init.h>
#include <hal/system.h>
#include <hal/thread.h>
#include <hal/timer.h>
#include <bus/exchange.h>

#include "fifo.h"

struct link_monitor {
    const char *label;

    duct_flow_index loop_quantity;
    uint8_t *packet_data;
    size_t  *packet_lens;

    size_t valid_epochs;

    duct_t *rx;
    duct_t *tx;

    duct_flow_index max_rate;
    size_t max_packet;

    bool validated;
};

static void link_monitor_clip(struct link_monitor *mon);

macro_define(LINK_MONITOR, m_ident, m_receive, m_transmit, m_max_rate, m_max_packet) {
    uint8_t symbol_join(m_ident, packet_data)[(m_max_rate) * (m_max_packet)];
    size_t symbol_join(m_ident, packet_lens)[m_max_rate];
    struct link_monitor m_ident = {
        .label = symbol_str(m_ident),
        .loop_quantity = 0,
        .packet_data = symbol_join(m_ident, packet_data),
        .packet_lens = symbol_join(m_ident, packet_lens),
        .valid_epochs = 0,
        .rx = &(m_receive),
        .tx = &(m_transmit),
        .max_rate = (m_max_rate),
        .max_packet = (m_max_packet),
        .validated = false,
    };
    CLIP_REGISTER(symbol_join(m_ident, clip), link_monitor_clip, &m_ident)
}

macro_define(LINK_MONITOR_SCHEDULE, m_ident) {
    CLIP_SCHEDULE(symbol_join(m_ident, clip), 100)
}

static void random_packet_chain(uint8_t *packet_data_out, size_t *packet_lens_out, size_t count, size_t max_len) {
    // debugf(DEBUG, "Generating packets...");
    assert(max_len >= 10 && max_len % sizeof(uint32_t) == 0);
    for (size_t i = 0; i < count; i++) {
        uint32_t value = lrand48();
        size_t packet_len = 1 + ((value & 1) ? ((value >> 1) % (max_len - 1)) : ((value >> 1) % 9u));
        assert(1 <= packet_len && packet_len <= max_len);
        uint32_t *packet_data_u32 = (uint32_t *) &packet_data_out[max_len * i];
        for (size_t j = 0; j < packet_len / 4; j++) {
            packet_data_u32[j] = mrand48();
        }
        packet_lens_out[i] = packet_len;
        // debugf(DEBUG, "[%d] => packet of size %lu", i, new_len);
    }
    // debugf(INFO, "Generated packet chain of length %d", packet_count);
}

static void link_monitor_clip(struct link_monitor *mon) {
    assert(mon != NULL);

    uint8_t recv_data[mon->max_packet];
    size_t  recv_len;

    duct_txn_t txn;
    duct_receive_prepare(&txn, mon->rx, 0);

    size_t count_successes = 0;
    for (duct_flow_index i = 0; i < mon->loop_quantity; i++) {
        recv_len = duct_receive_message(&txn, recv_data, NULL);
        if (recv_len == 0) {
            break;
        }
        if (recv_len != mon->packet_lens[i]) {
            abortf("Packet mismatch: expected len=%zu, received len=%zu.", mon->packet_lens[i], recv_len);
        }
        if (memcmp(&mon->packet_data[mon->max_packet * i], recv_data, recv_len) != 0) {
            abortf("Packet mismatch: data was not indentical despite indentical lengths.");
        }
        count_successes++;
    }

    debugf(TRACE, "[%s] Packet flow: %zu/%u packets (valid_epochs=%zu).",
           mon->label, count_successes, mon->loop_quantity, mon->valid_epochs);

    recv_len = duct_receive_message(&txn, NULL, NULL);
    if (recv_len > 0) {
        abortf("[%s] Received unexpected packet of length %zu", mon->label, recv_len);
    }

    if (count_successes != mon->loop_quantity) {
        if (mon->valid_epochs == 0) {
            if (count_successes == 0) {
                // still not initialized; that's fine!
            } else {
                // might have JUST been initialized, and only some messages went through! that's okay.
                mon->valid_epochs = 1;
            }
        } else {
            abortf("[%s] Experienced invalid epoch (%zu/%u) after %zu valid epochs; should keep working!",
                   mon->label, count_successes, mon->loop_quantity, mon->valid_epochs);
        }
    } else if (mon->loop_quantity > 0) {
        mon->valid_epochs++;
        if (mon->valid_epochs >= 1000 && !mon->validated) {
            debugf(INFO, "[%s] Reached %zu valid epochs in link monitor; marking validated.",
                   mon->label, mon->valid_epochs);
            atomic_store_relaxed(mon->validated, true);
        }
    }

    duct_receive_commit(&txn);

    mon->loop_quantity = rand() % (mon->max_rate + 1);
    random_packet_chain(mon->packet_data, mon->packet_lens, mon->loop_quantity, mon->max_packet);

    duct_send_prepare(&txn, mon->tx, 0);

    for (duct_flow_index i = 0; i < mon->loop_quantity; i++) {
        if (!duct_send_allowed(&txn)) {
            abortf("Unable to transmit message at a point where it should be possible.");
        }
        duct_send_message(&txn, &mon->packet_data[mon->max_packet * i], mon->packet_lens[i], 0 /* no timestamp */);
    }

    duct_send_commit(&txn);
}

/* TODO: USE SOMETHING LIKE THIS
static bool compare_packets(uint8_t *baseline, size_t baseline_len, uint8_t *actual, size_t actual_len) {
    size_t i, mismatches = 0;
    for (i = 0; i < baseline_len && i < actual_len; i++) {
        if (baseline[i] != actual[i]) {
            mismatches++;
        }
    }
    if (mismatches > 0) {
        debugf(CRITICAL, "Mismatch: out of %lu bytes, found %lu mismatches", i, mismatches);
    }
    if (baseline_len != actual_len) {
        debugf(CRITICAL, "Mismatch: packet length should have been %lu, but found %lu", baseline_len, actual_len);
        return false;
    }
    return mismatches == 0;
}
*/

#define TESTING_ASSEMBLY(t_ident, t_max_flow, t_max_packet)                                                           \
    FIFO_REGISTER("./fwfifo");                                                                                        \
    const fw_link_options_t t_ident ## _left_options = {                                                              \
        .label = "left",                                                                                              \
        .path = "./fwfifo",                                                                                           \
        .flags = FW_FLAG_FIFO_PROD,                                                                                   \
    };                                                                                                                \
    const fw_link_options_t t_ident ## _right_options = {                                                             \
        .label = "right",                                                                                             \
        .path = "./fwfifo",                                                                                           \
        .flags = FW_FLAG_FIFO_CONS,                                                                                   \
    };                                                                                                                \
    DUCT_REGISTER(t_ident ## _left_rx_duct,  1, 1, (t_max_flow) * 2, t_max_packet, DUCT_SENDER_FIRST);                \
    DUCT_REGISTER(t_ident ## _left_tx_duct,  1, 1, (t_max_flow) * 2, t_max_packet, DUCT_RECEIVER_FIRST);              \
    DUCT_REGISTER(t_ident ## _right_rx_duct, 1, 1, (t_max_flow) * 2, t_max_packet, DUCT_SENDER_FIRST);                \
    DUCT_REGISTER(t_ident ## _right_tx_duct, 1, 1, (t_max_flow) * 2, t_max_packet, DUCT_RECEIVER_FIRST);              \
    FAKEWIRE_EXCHANGE_REGISTER(t_ident ## _left,  t_ident ## _left_options,                                           \
                               t_ident ## _left_rx_duct,  t_ident ## _left_tx_duct,  t_max_flow, t_max_packet);       \
    FAKEWIRE_EXCHANGE_REGISTER(t_ident ## _right, t_ident ## _right_options,                                          \
                               t_ident ## _right_rx_duct, t_ident ## _right_tx_duct, t_max_flow, t_max_packet);       \
    LINK_MONITOR(t_ident ## _mon_l2r, t_ident ## _right_rx_duct, t_ident ## _left_tx_duct, t_max_flow, t_max_packet); \
    LINK_MONITOR(t_ident ## _mon_r2l, t_ident ## _left_rx_duct, t_ident ## _right_tx_duct, t_max_flow, t_max_packet); \
    static bool t_ident ## _is_done(void) {                                                                           \
        return atomic_load_relaxed(t_ident ## _mon_l2r.validated)                                                     \
            && atomic_load_relaxed(t_ident ## _mon_r2l.validated);                                                    \
    }

#define TESTING_ASSEMBLY_SCHEDULE(t_ident)                                                                            \
    FAKEWIRE_EXCHANGE_TRANSMIT_SCHEDULE(t_ident ## _left)                                                             \
    FAKEWIRE_EXCHANGE_TRANSMIT_SCHEDULE(t_ident ## _right)                                                            \
    FAKEWIRE_EXCHANGE_RECEIVE_SCHEDULE(t_ident ## _right)                                                             \
    FAKEWIRE_EXCHANGE_RECEIVE_SCHEDULE(t_ident ## _left)                                                              \
    LINK_MONITOR_SCHEDULE(t_ident ## _mon_l2r)                                                                        \
    LINK_MONITOR_SCHEDULE(t_ident ## _mon_r2l)

static void init_random(void) {
    srand48(31415);
}
PROGRAM_INIT(STAGE_RAW, init_random);

TASK_PROTO(task_main);

TESTING_ASSEMBLY(validator, 5, 500);

SCHEDULE_PARTITION_ORDER() {
    TESTING_ASSEMBLY_SCHEDULE(validator)
    TASK_SCHEDULE(task_main, 100)
    SYSTEM_MAINTENANCE_SCHEDULE()
}

static void test_main(void) {
    debugf(INFO, "Waiting for test to complete...");

    // wait up to ~five seconds (adjusted for actual number of cycles)
    uint32_t yields = 2000;
    while (yields > 0 && !validator_is_done()) {
        task_yield();
        yields--;
    }

    if (!validator_is_done()) {
        abortf("Monitors did not report success by end of timeout period.");
    }

    debugf(INFO, "Test complete!");
    exit(0);
}

TASK_REGISTER(task_main, test_main, NULL, NOT_RESTARTABLE);
