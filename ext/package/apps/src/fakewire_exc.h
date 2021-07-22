#ifndef APP_FAKEWIRE_EXC_H
#define APP_FAKEWIRE_EXC_H

#include "fakewire_link.h"
#include "thread.h"

// custom exchange protocol
typedef enum fw_exchange_state_e {
    FW_EXC_INVALID = 0,  // should never be set to this value during normal execution
    FW_EXC_DISCONNECTED,
    FW_EXC_HANDSHAKING,
    FW_EXC_OPERATING,
} fw_exchange_state;

typedef struct fw_exchange_st {
    const char *label;

    fw_exchange_state state;
    fw_link_t         io_port;
    fw_receiver_t     link_interface;

    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    bool tx_busy;
    bool detaching;

    pthread_t flowtx_thread;

    // these three are stored in network order, not host order
    uint32_t primary_id;
    uint32_t secondary_id;
    bool needs_send_secondary_handshake;
    bool sent_primary_handshake;
    uint32_t sent_primary_id;

    uint8_t *inbound_buffer;
    size_t   inbound_buffer_offset;
    size_t   inbound_buffer_max;
    bool     inbound_read_done;
    bool     recv_in_progress;

    bool     has_sent_fct;

    bool     remote_sent_fct;
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
