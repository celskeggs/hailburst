#ifndef FSW_FAKEWIRE_EXCHANGE_H
#define FSW_FAKEWIRE_EXCHANGE_H

#include <hal/thread.h>
#include <synch/chart.h>
#include <bus/link.h>
#include <bus/switch.h>

enum {
    EXCHANGE_QUEUE_DEPTH = 16,
};

// custom exchange protocol
enum exchange_state {
    FW_EXC_INVALID = 0, // should never be set to this value during normal execution
    FW_EXC_CONNECTING,  // waiting for primary handshake, or, if none received, will send primary handshake
    FW_EXC_HANDSHAKING, // waiting for secondary handshake, or, if primary received, will reset
    FW_EXC_OPERATING,   // received a valid non-conflicting handshake
};

enum receive_state {
    FW_RECV_LISTENING = 0, // waiting for Start-of-Packet character
    FW_RECV_RECEIVING,     // receiving data body of packet
    FW_RECV_OVERFLOWED,    // received data too large for buffer; waiting for end before discarding
};

enum transmit_state {
    FW_TXMIT_IDLE = 0, // waiting for a new packet to be ready to send
    FW_TXMIT_HEADER,   // waiting to transmit START_PACKET symbol
    FW_TXMIT_BODY,     // waiting to transmit data characters in packet
    FW_TXMIT_FOOTER,   // waiting to transmit END_PACKET symbol
};

// this structure is reinitialized every time the exchange task restarts
typedef struct {
    const struct fw_exchange_st *conf;

    fw_encoder_t encoder;
    fw_decoder_t decoder;

    enum exchange_state exc_state;
    enum receive_state  recv_state;
    enum transmit_state txmit_state;

    uint64_t next_timeout;

    uint32_t send_handshake_id; // generated handshake ID if in HANDSHAKING mode
    uint32_t recv_handshake_id; // received handshake ID

    bool send_primary_handshake;
    bool send_secondary_handshake;

    uint32_t fcts_sent;
    uint32_t fcts_rcvd;
    uint32_t pkts_sent;
    uint32_t pkts_rcvd;
    bool     resend_fcts;
    bool     resend_pkts;

    struct io_rx_ent *read_entry;
    struct io_rx_ent *write_entry;
    size_t write_offset;
} exchange_instance_t;

typedef const struct fw_exchange_st {
    const char *label;

    exchange_instance_t *instance;

    thread_t exchange_task;

    chart_t *transmit_chart; // client: exchange_thread, server: link driver, struct io_tx_ent
    chart_t *receive_chart;  // client: link driver, server: exchange_thread, struct io_rx_ent
    chart_t *read_chart;     // client: exchange_thread, server: switch task, struct io_rx_ent
    chart_t *write_chart;    // client: switch task, server: exchange_thread, struct io_rx_ent (yes, actually!)
} fw_exchange_t;

void fakewire_exc_notify(fw_exchange_t *fwe);
void fakewire_exc_init_internal(fw_exchange_t *fwe);
void fakewire_exc_exchange_loop(fw_exchange_t *fwe);

#define FAKEWIRE_EXCHANGE_REGISTER(e_ident, e_link_options, e_read_chart, e_write_chart)                 \
    extern fw_exchange_t e_ident;                                                                        \
    TASK_REGISTER(e_ident ## _task, "fw_exc_thread", fakewire_exc_exchange_loop, &e_ident, RESTARTABLE); \
    CHART_CLIENT_NOTIFY(e_read_chart, task_rouse, &e_ident ## _task);                                    \
    CHART_SERVER_NOTIFY(e_write_chart, task_rouse, &e_ident ## _task);                                   \
    CHART_REGISTER(e_ident ## _transmit_chart, 1024, EXCHANGE_QUEUE_DEPTH);                              \
    CHART_CLIENT_NOTIFY(e_ident ## _transmit_chart, task_rouse, &e_ident ## _task);                      \
    CHART_REGISTER(e_ident ## _receive_chart, 1024, EXCHANGE_QUEUE_DEPTH);                               \
    CHART_SERVER_NOTIFY(e_ident ## _receive_chart, task_rouse, &e_ident ## _task);                       \
    FAKEWIRE_LINK_REGISTER(e_ident ## _io_port, e_link_options,                                          \
                           e_ident ## _receive_chart, e_ident ## _transmit_chart,                        \
                           EXCHANGE_QUEUE_DEPTH, EXCHANGE_QUEUE_DEPTH);                                  \
    exchange_instance_t e_ident ## _instance;                                                            \
    fw_exchange_t e_ident = {                                                                            \
        .label = (e_link_options).label,                                                                 \
        .instance = &e_ident ## _instance,                                                               \
        .exchange_task = &e_ident ## _task,                                                              \
        .transmit_chart = &e_ident ## _transmit_chart,                                                   \
        .receive_chart = &e_ident ## _receive_chart,                                                     \
        .read_chart = &e_read_chart,                                                                     \
        .write_chart = &e_write_chart,                                                                   \
    }

#define FAKEWIRE_EXCHANGE_ON_SWITCH(e_ident, e_link_options, e_switch, e_switch_port)                    \
    CHART_REGISTER(e_ident ## _read_chart, 0x1100, 10);                                                  \
    CHART_REGISTER(e_ident ## _write_chart, 0x1100, 10);                                                 \
    FAKEWIRE_EXCHANGE_REGISTER(e_ident, e_link_options,                                                  \
                               e_ident ## _read_chart, e_ident ## _write_chart);                         \
    SWITCH_PORT(e_switch, e_switch_port, e_ident ## _read_chart, e_ident ## _write_chart)

#define FAKEWIRE_EXCHANGE_SCHEDULE(e_ident)     \
    FAKEWIRE_LINK_SCHEDULE(e_ident ## _io_port) \
    TASK_SCHEDULE(e_ident ## _task, 150)        \
    FAKEWIRE_LINK_SCHEDULE(e_ident ## _io_port) \
    TASK_SCHEDULE(e_ident ## _task, 150)

#endif /* FSW_FAKEWIRE_EXCHANGE_H */
