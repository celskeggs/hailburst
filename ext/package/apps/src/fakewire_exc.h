#ifndef APP_FAKEWIRE_EXC_H
#define APP_FAKEWIRE_EXC_H

#include "fakewire_link.h"
#include "thread.h"

// simplified/one-shot version of SpaceWire exchange protocol
typedef enum fw_exchange_state_e {
    FW_EXC_DISCONNECTED = 1,
    FW_EXC_STARTED,
    FW_EXC_CONNECTING,
    FW_EXC_RUN,
    FW_EXC_ERRORED,
} fw_exchange_state;

typedef struct fw_exchange_st {
    const char *label;

    fw_exchange_state state;
    fw_link_t         io_port;

    pthread_mutex_t mutex;
    pthread_cond_t  cond;

    pthread_t reader_thread;
    pthread_t writer_thread;

    uint8_t *inbound_buffer;
    size_t   inbound_buffer_offset;
    size_t   inbound_buffer_max;
    bool     inbound_read_done;

    bool     has_sent_fct;

    uint8_t *outbound_buffer;
    size_t   outbound_buffer_offset;
    size_t   outbound_buffer_max;
    bool     outbound_write_done;

    bool     remote_sent_fct;
} fw_exchange_t;

// these two functions are not threadsafe; the current thread must be the only thread accessing fwe
void fakewire_exc_init(fw_exchange_t *fwe, const char *label);
// in addition, destroy must only be called if the exchange was never attached, or if it was already detached.
void fakewire_exc_destroy(fw_exchange_t *fwe);

// the remaining functions ARE threadsafe

void fakewire_exc_attach(fw_exchange_t *fwe, const char *path, int flags);
void fakewire_exc_detach(fw_exchange_t *fwe);

// actual length of packet is returned (>= 0), or else a negative number to indicate an error.
// if the buffer is too small for the packet, only up to packet_max bytes are written, but the return value includes the
// truncated portion in the length.
ssize_t fakewire_exc_read(fw_exchange_t *fwe, uint8_t *packet_out, size_t packet_max);
// zero returned on success, negative for error
int fakewire_exc_write(fw_exchange_t *fwe, uint8_t *packet_in, size_t packet_len);

#endif /* APP_FAKEWIRE_EXC_H */
