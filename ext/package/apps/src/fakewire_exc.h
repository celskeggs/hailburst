#ifndef APP_FAKEWIRE_EXC_H
#define APP_FAKEWIRE_EXC_H

#include "fakewire_link.h"
#include "thread.h"

// custom exchange protocol
typedef enum {
    FW_EXC_INVALID = 0,  // should never be set to this value during normal execution
    FW_EXC_DISCONNECTED,
    FW_EXC_CONNECTING,  // waiting for primary handshake, or, if none received, will send primary handshake
    FW_EXC_HANDSHAKING, // waiting for secondary handshake, or, if primary received, will reset
    FW_EXC_OPERATING,   // received a valid non-conflicting handshake
} fw_exchange_state;

typedef struct fw_exchange_st {
    const char *label;

    fw_exchange_state state;
    fw_link_t         io_port;
    fw_receiver_t     link_interface;

    mutex_t mutex;
    cond_t  cond;
    bool tx_busy;
    bool detaching;

    thread_t flowtx_thread;

    uint32_t send_handshake_id; // generated handshake ID if in HANDSHAKING mode
    uint32_t recv_handshake_id; // received handshake ID
    bool     send_secondary_handshake;

    uint32_t fcts_sent;
    uint32_t fcts_rcvd;
    uint32_t pkts_sent;
    uint32_t pkts_rcvd;

    uint8_t *inbound_buffer;
    size_t   inbound_buffer_offset;
    size_t   inbound_buffer_max;
    bool     inbound_read_done;
    bool     recv_in_progress;
} fw_exchange_t;

// these two functions are not threadsafe; the current thread must be the only thread accessing fwe
void fakewire_exc_init(fw_exchange_t *fwe, const char *label);
// in addition, destroy must only be called if the exchange is not currently attached
void fakewire_exc_destroy(fw_exchange_t *fwe);

// the remaining functions ARE threadsafe

// returns 0 if successfully initialized, -1 if an I/O error prevented initialization
int fakewire_exc_attach(fw_exchange_t *fwe, const char *path, int flags);
void fakewire_exc_detach(fw_exchange_t *fwe);

// actual length of packet is returned (>= 0), or else a negative number to indicate an error.
// if the buffer is too small for the packet, only up to packet_max bytes are written, but the return value includes the
// truncated portion in the length.
ssize_t fakewire_exc_read(fw_exchange_t *fwe, uint8_t *packet_out, size_t packet_max);
// zero returned on success, negative for error
int fakewire_exc_write(fw_exchange_t *fwe, uint8_t *packet_in, size_t packet_len);

#endif /* APP_FAKEWIRE_EXC_H */
