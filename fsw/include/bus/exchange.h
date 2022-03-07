#ifndef FSW_FAKEWIRE_EXCHANGE_H
#define FSW_FAKEWIRE_EXCHANGE_H

#include <hal/thread.h>
#include <bus/link.h>
#include <bus/switch.h>

#define EXCHANGE_REPLICAS 1

enum {
    EXCHANGE_QUEUE_DEPTH = 16,
    MAX_OUTSTANDING_TOKENS = 10,
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

    enum exchange_state exc_state;
    enum receive_state  recv_state;

    uint32_t countdown_timeout;

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

    size_t   read_offset;
    uint64_t read_timestamp;
    bool     write_needs_error;
} exchange_instance_t;

typedef const struct fw_exchange_st {
    uint8_t exchange_replica_id;
    const char *label;

    exchange_instance_t *instance;

    fw_encoder_t *encoder;
    fw_decoder_t *decoder;

    size_t   buffers_length;
    uint8_t *read_buffer;
    uint8_t *write_buffer;

    duct_t *read_duct;     // sender: exchange_thread, recipient: switch task
    duct_t *write_duct;    // sender: switch task, recipient: exchange_thread
} fw_exchange_t;

void fakewire_exc_notify(fw_exchange_t *fwe);
void fakewire_exc_init_internal(fw_exchange_t *fwe);
void fakewire_exc_tx_clip(fw_exchange_t *fwe);
void fakewire_exc_rx_clip(fw_exchange_t *fwe);

macro_define(FAKEWIRE_EXCHANGE_REGISTER,
             e_ident, e_link_options, e_read_duct, e_write_duct, e_max_flow, e_buf_size) {
    static_assert(e_max_flow <= EXCHANGE_QUEUE_DEPTH, "exchange is not guaranteed to be able to transmit this fast");
    /* in order to continously transmit N packets per cycle, there must be able to be 2N packets outstanding */
    static_assert((e_max_flow) * 2 <= MAX_OUTSTANDING_TOKENS, "exchange protocol cannot transmit this fast");
    DUCT_REGISTER(symbol_join(e_ident, transmit_duct), EXCHANGE_REPLICAS, 1,
                  1, (e_max_flow) * (e_buf_size) + 1024, DUCT_SENDER_FIRST);
    DUCT_REGISTER(symbol_join(e_ident, receive_duct),  1, EXCHANGE_REPLICAS,
                  1, (e_max_flow) * (e_buf_size) + 1024, DUCT_SENDER_FIRST);
    FAKEWIRE_LINK_REGISTER(symbol_join(e_ident, io_port), e_link_options,
                           symbol_join(e_ident, receive_duct), symbol_join(e_ident, transmit_duct),
                           (e_max_flow) * (e_buf_size) + 1024);
    static_repeat(EXCHANGE_REPLICAS, replica_id) {
        FAKEWIRE_ENCODER_REGISTER(symbol_join(e_ident, encoder, replica_id),
                                  symbol_join(e_ident, transmit_duct), replica_id, (e_max_flow) * (e_buf_size) + 1024);
        FAKEWIRE_DECODER_REGISTER(symbol_join(e_ident, decoder, replica_id),
                                  symbol_join(e_ident, receive_duct),  replica_id, (e_max_flow) * (e_buf_size) + 1024);
        uint8_t symbol_join(e_ident, read_buffer,  replica_id)[e_buf_size];
        uint8_t symbol_join(e_ident, write_buffer, replica_id)[e_buf_size];
        exchange_instance_t symbol_join(e_ident, instance, replica_id);
        fw_exchange_t symbol_join(e_ident, replica_id) = {
            .exchange_replica_id = replica_id,
            .label = (e_link_options).label,
            .instance = &symbol_join(e_ident, instance, replica_id),
            .encoder  = &symbol_join(e_ident, encoder,  replica_id),
            .decoder  = &symbol_join(e_ident, decoder,  replica_id),
            .buffers_length = (e_buf_size),
            .read_buffer  = symbol_join(e_ident, read_buffer,  replica_id),
            .write_buffer = symbol_join(e_ident, write_buffer, replica_id),
            .read_duct  = &e_read_duct,
            .write_duct = &e_write_duct,
        };
        CLIP_REGISTER(symbol_join(e_ident, tx_clip, replica_id),
                      fakewire_exc_tx_clip, &symbol_join(e_ident, replica_id));
        CLIP_REGISTER(symbol_join(e_ident, rx_clip, replica_id),
                      fakewire_exc_rx_clip, &symbol_join(e_ident, replica_id));
    }
}

macro_define(FAKEWIRE_EXCHANGE_ON_SWITCHES,
             e_ident, e_link_options, e_switch_in, e_switch_out, e_switch_port, e_max_flow, e_max_size) {
    DUCT_REGISTER(symbol_join(e_ident, read_duct),  EXCHANGE_REPLICAS, SWITCH_REPLICAS,
                  (e_max_flow) * 2, e_max_size, DUCT_SENDER_FIRST);
    DUCT_REGISTER(symbol_join(e_ident, write_duct), SWITCH_REPLICAS, EXCHANGE_REPLICAS,
                  (e_max_flow) * 2, e_max_size, DUCT_RECEIVER_FIRST);
    FAKEWIRE_EXCHANGE_REGISTER(e_ident, e_link_options,
                               symbol_join(e_ident, read_duct), symbol_join(e_ident, write_duct),
                               e_max_flow, e_max_size);
    SWITCH_PORT_INBOUND(e_switch_in, e_switch_port, symbol_join(e_ident, read_duct));
    SWITCH_PORT_OUTBOUND(e_switch_out, e_switch_port, symbol_join(e_ident, write_duct))
}

macro_define(FAKEWIRE_EXCHANGE_TRANSMIT_SCHEDULE, e_ident) {
    static_repeat(EXCHANGE_REPLICAS, replica_id) {
        CLIP_SCHEDULE(symbol_join(e_ident, tx_clip, replica_id), 116)
    }
    FAKEWIRE_LINK_SCHEDULE_TRANSMIT(symbol_join(e_ident, io_port))
}

macro_define(FAKEWIRE_EXCHANGE_RECEIVE_SCHEDULE, e_ident) {
    FAKEWIRE_LINK_SCHEDULE_RECEIVE(symbol_join(e_ident, io_port))
    static_repeat(EXCHANGE_REPLICAS, replica_id) {
        CLIP_SCHEDULE(symbol_join(e_ident, rx_clip, replica_id), 49)
    }
}

macro_define(FAKEWIRE_EXCHANGE_SCHEDULE, e_ident) {
    FAKEWIRE_EXCHANGE_TRANSMIT_SCHEDULE(e_ident)
    FAKEWIRE_EXCHANGE_RECEIVE_SCHEDULE(e_ident)
}

#endif /* FSW_FAKEWIRE_EXCHANGE_H */
