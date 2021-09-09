#ifndef FSW_FAKEWIRE_EXCHANGE_H
#define FSW_FAKEWIRE_EXCHANGE_H

#include <hal/thread.h>
#include <fsw/fakewire/link.h>

// custom exchange protocol
typedef enum {
    FW_EXC_INVALID = 0, // should never be set to this value during normal execution
    FW_EXC_CONNECTING,  // waiting for primary handshake, or, if none received, will send primary handshake
    FW_EXC_HANDSHAKING, // waiting for secondary handshake, or, if primary received, will reset
    FW_EXC_OPERATING,   // received a valid non-conflicting handshake
} fw_exchange_state;

typedef enum {
    FW_RECV_PREPARING = 0, // waiting for operating mode to be reached and for FCT to be sent
    FW_RECV_LISTENING,     // waiting for Start-of-Packet character
    FW_RECV_RECEIVING,     // receiving data body of packet
    FW_RECV_OVERFLOWED,    // received data too large for buffer; waiting for end before discarding
    FW_RECV_CALLBACK,      // full packet has been received; waiting for callback to complete
} fw_exchange_recv_state;

typedef void (*fakewire_exc_read_cb)(void *param, uint8_t *packet_data, size_t packet_length);

typedef struct {
    fw_link_options_t link_options;
    // receive settings
    size_t               recv_max_size;
    fakewire_exc_read_cb recv_callback;
    void                *recv_param;
} fw_exchange_options_t;

typedef struct fw_exchange_st {
    fw_exchange_options_t options;

    fw_exchange_state state;
    fw_link_t         io_port;
    fw_receiver_t     link_interface;

    mutex_t mutex; // protects fields
    cond_t  cond;

    // do not block 'mutex' on acquiring 'tx_busy'
    // do not block 'tx_busy' on waiting for the condition variable
    mutex_t tx_busy;

    thread_t flowtx_thread;
    thread_t reader_thread;

    uint32_t send_handshake_id; // generated handshake ID if in HANDSHAKING mode
    uint32_t recv_handshake_id; // received handshake ID
    bool     send_secondary_handshake;

    uint32_t fcts_sent;
    uint32_t fcts_rcvd;
    uint32_t pkts_sent;
    uint32_t pkts_rcvd;

    uint8_t               *recv_buffer; // allocated size: options.recv_max_size
    fw_exchange_recv_state recv_state;
    size_t                 recv_offset;
} fw_exchange_t;

// returns 0 if successfully initialized, -1 if an I/O error prevented initialization
int fakewire_exc_init(fw_exchange_t *fwe, fw_exchange_options_t opts);

void fakewire_exc_write(fw_exchange_t *fwe, uint8_t *packet_in, size_t packet_len);

#endif /* FSW_FAKEWIRE_EXCHANGE_H */
