#ifndef APP_RMAP_H
#define APP_RMAP_H

#include <stdint.h>
#include <stdbool.h>
#include "fakewire_exc.h"
#include "thread.h"

#define RMAP_MAX_PATH (12)
#define RMAP_MAX_DATA_LEN (0x00FFFFFF)

typedef struct {
    uint8_t *path_bytes;
    uint8_t num_path_bytes;
    uint8_t logical_address;
} rmap_path_t;

typedef struct {
    rmap_path_t destination;
    rmap_path_t source;
    uint8_t dest_key;
} rmap_addr_t;

typedef enum {
    RF_RESERVED    = 0x80,
    RF_COMMAND     = 0x40,
    RF_WRITE       = 0x20,
    RF_VERIFY      = 0x10,
    RF_ACKNOWLEDGE = 0x08,
    RF_INCREMENT   = 0x04,
    RF_SOURCEPATH  = 0x03,
} rmap_flags_t;

typedef enum {
    RS_OK = 0x00,
    RS_REMOTE_ERR_MIN = 0x01,
    RS_REMOTE_ERR_MAX = 0xFF,
    RS_EXCHANGE_DOWN = 0x100,
    RS_RECVLOOP_STOPPED = 0x101,
    RS_DATA_TRUNCATED = 0x102,
    RS_TRANSACTION_TIMEOUT = 0x103,
} rmap_status_t;

typedef struct rmap_context_st rmap_context_t;

typedef struct {
    uint16_t next_txn_id;
    fw_exchange_t *exc;

    bool hit_recv_err;

    size_t scratch_size;
    uint8_t *scratch_buffer;

    mutex_t pending_mutex;
    cond_t  pending_cond;
    rmap_context_t *pending_first;

    thread_t monitor_thread;
} rmap_monitor_t;

typedef struct rmap_context_st {
    rmap_monitor_t *monitor;

    size_t scratch_size;
    uint8_t *scratch_buffer;

    bool is_pending;
    uint8_t txn_flags;
    void *read_output;
    uint32_t read_max_length;
    uint32_t read_actual_length;
    bool has_received;
    uint8_t received_status;
    uint16_t pending_txn_id;
    rmap_addr_t *pending_routing;
    rmap_context_t *pending_next;
} rmap_context_t;

void rmap_init_monitor(rmap_monitor_t *mon, fw_exchange_t *exc, size_t max_read_length);
void rmap_init_context(rmap_context_t *context, rmap_monitor_t *mon, size_t max_write_length);
rmap_status_t rmap_write(rmap_context_t *context, rmap_addr_t *routing, rmap_flags_t flags,
                         uint8_t ext_addr, uint32_t main_addr, size_t data_length, void *data);
rmap_status_t rmap_read(rmap_context_t *context, rmap_addr_t *routing, rmap_flags_t flags,
                        uint8_t ext_addr, uint32_t main_addr, size_t *data_length, void *data_out);

#endif /* APP_RMAP_H */