#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <hal/thread.h>
#include <fsw/debug.h>
#include <fsw/fakewire/rmap.h>

// #define DEBUGTXN

enum {
    // time out transactions after two milliseconds, which is nearly 4x the average time for a transaction.
    RMAP_TIMEOUT_NS = 2 * 1000 * 1000,
};

#define SCRATCH_MARGIN_WRITE (RMAP_MAX_PATH + 4 + RMAP_MAX_PATH + 12 + 1)
#define SCRATCH_MARGIN_READ (12 + 1)  // for read replies
#define PROTOCOL_RMAP (0x01)

static void *rmap_monitor_recvloop(void *mon_opaque);

void rmap_init_monitor(rmap_monitor_t *mon, fw_exchange_t *exc, size_t max_read_length) {
    assert(max_read_length <= RMAP_MAX_DATA_LEN);
    mon->next_txn_id = 1;
    mon->exc = exc;
    mon->pending_first = NULL;
    mon->hit_recv_err = false;

    mon->scratch_size = max_read_length + SCRATCH_MARGIN_READ;
    mon->scratch_buffer = malloc(mon->scratch_size);
    assert(mon->scratch_buffer != NULL);

    mutex_init(&mon->pending_mutex);

    thread_create(&mon->monitor_thread, "rmap_monitor", PRIORITY_SERVERS, rmap_monitor_recvloop, mon);
}

static bool rmap_has_txn_in_progress(rmap_monitor_t *mon, uint16_t txn_id) {
    // assumes pending_mutex is locked
    if (txn_id == 0) {
        return true;
    }
    for (rmap_context_t *cur = mon->pending_first; cur != NULL; cur = cur->pending_next) {
        assert(cur->is_pending);
        assert(cur->monitor == mon);
        if (cur->pending_txn_id == txn_id) {
            return true;
        }
    }
    return false;
}

// assumes pending_mutex is locked; it must not be unlocked until the generated txn_id is installed in the rmap_context_t!
static uint16_t rmap_next_txn(rmap_monitor_t *mon) {
    uint16_t txn_id;
    int cycles = 0;
    // search for a txn_id not in use
    while (rmap_has_txn_in_progress(mon, mon->next_txn_id)) {
        mon->next_txn_id++;
        // don't loop forever
        assert(++cycles <= 65536);
    }
    // advance so that our next search is likely to complete instantly
    txn_id = mon->next_txn_id++;
    return txn_id;
}

void rmap_init_context(rmap_context_t *context, rmap_monitor_t *mon, size_t max_write_length) {
    assert(max_write_length <= RMAP_MAX_DATA_LEN);
    context->monitor = mon;
    context->scratch_size = max_write_length + SCRATCH_MARGIN_WRITE;
    context->scratch_buffer = malloc(context->scratch_size);
    assert(context->scratch_buffer != NULL);
    context->is_pending = false;
    context->pending_next = NULL;
    semaphore_init(&context->on_complete);
}

static void rmap_encode_source_path(uint8_t **out, rmap_path_t *path) {
    // if we start with zeros, and aren't just a single zero, this CANNOT be encoded in the RMAP encoding scheme.
    assert(!(path->num_path_bytes > 1 && path->path_bytes[0] == 0));
    // output some zeros as padding
    size_t nzeros = 3 - ((path->num_path_bytes + 3) % 4);
    // make sure that we don't have too many bytes to fit
    assert(nzeros + path->num_path_bytes <= RMAP_MAX_PATH);
    // and then output the last
    *out += nzeros;
    memcpy(*out, path->path_bytes, path->num_path_bytes);
    *out += path->num_path_bytes;
}

static uint8_t rmapCrcTable[256] = {
	0x00, 0x91, 0xe3, 0x72, 0x07, 0x96, 0xe4, 0x75,
	0x0e, 0x9f, 0xed, 0x7c, 0x09, 0x98, 0xea, 0x7b,
	0x1c, 0x8d, 0xff, 0x6e, 0x1b, 0x8a, 0xf8, 0x69,
	0x12, 0x83, 0xf1, 0x60, 0x15, 0x84, 0xf6, 0x67,
	0x38, 0xa9, 0xdb, 0x4a, 0x3f, 0xae, 0xdc, 0x4d,
	0x36, 0xa7, 0xd5, 0x44, 0x31, 0xa0, 0xd2, 0x43,
	0x24, 0xb5, 0xc7, 0x56, 0x23, 0xb2, 0xc0, 0x51,
	0x2a, 0xbb, 0xc9, 0x58, 0x2d, 0xbc, 0xce, 0x5f,
	0x70, 0xe1, 0x93, 0x02, 0x77, 0xe6, 0x94, 0x05,
	0x7e, 0xef, 0x9d, 0x0c, 0x79, 0xe8, 0x9a, 0x0b,
	0x6c, 0xfd, 0x8f, 0x1e, 0x6b, 0xfa, 0x88, 0x19,
	0x62, 0xf3, 0x81, 0x10, 0x65, 0xf4, 0x86, 0x17,
	0x48, 0xd9, 0xab, 0x3a, 0x4f, 0xde, 0xac, 0x3d,
	0x46, 0xd7, 0xa5, 0x34, 0x41, 0xd0, 0xa2, 0x33,
	0x54, 0xc5, 0xb7, 0x26, 0x53, 0xc2, 0xb0, 0x21,
	0x5a, 0xcb, 0xb9, 0x28, 0x5d, 0xcc, 0xbe, 0x2f,
	0xe0, 0x71, 0x03, 0x92, 0xe7, 0x76, 0x04, 0x95,
	0xee, 0x7f, 0x0d, 0x9c, 0xe9, 0x78, 0x0a, 0x9b,
	0xfc, 0x6d, 0x1f, 0x8e, 0xfb, 0x6a, 0x18, 0x89,
	0xf2, 0x63, 0x11, 0x80, 0xf5, 0x64, 0x16, 0x87,
	0xd8, 0x49, 0x3b, 0xaa, 0xdf, 0x4e, 0x3c, 0xad,
	0xd6, 0x47, 0x35, 0xa4, 0xd1, 0x40, 0x32, 0xa3,
	0xc4, 0x55, 0x27, 0xb6, 0xc3, 0x52, 0x20, 0xb1,
	0xca, 0x5b, 0x29, 0xb8, 0xcd, 0x5c, 0x2e, 0xbf,
	0x90, 0x01, 0x73, 0xe2, 0x97, 0x06, 0x74, 0xe5,
	0x9e, 0x0f, 0x7d, 0xec, 0x99, 0x08, 0x7a, 0xeb,
	0x8c, 0x1d, 0x6f, 0xfe, 0x8b, 0x1a, 0x68, 0xf9,
	0x82, 0x13, 0x61, 0xf0, 0x85, 0x14, 0x66, 0xf7,
	0xa8, 0x39, 0x4b, 0xda, 0xaf, 0x3e, 0x4c, 0xdd,
	0xa6, 0x37, 0x45, 0xd4, 0xa1, 0x30, 0x42, 0xd3,
	0xb4, 0x25, 0x57, 0xc6, 0xb3, 0x22, 0x50, 0xc1,
	0xba, 0x2b, 0x59, 0xc8, 0xbd, 0x2c, 0x5e, 0xcf,
};

static uint8_t rmap_crc8(uint8_t *bytes, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc = rmapCrcTable[crc^bytes[i]];
    }
    return crc;
}

// Returns status code.
rmap_status_t rmap_write(rmap_context_t *context, rmap_addr_t *routing, rmap_flags_t flags,
                         uint8_t ext_addr, uint32_t main_addr, size_t data_length, void *data) {
    // make sure we didn't get any null pointers
    assert(context != NULL && routing != NULL && data != NULL);
    // make sure we have enough space to buffer this much data in scratch memory
    assert(0 < data_length && data_length <= RMAP_MAX_DATA_LEN && data_length + SCRATCH_MARGIN_WRITE <= context->scratch_size);
    // make sure flags are valid
    assert(flags == (flags & (RF_VERIFY | RF_ACKNOWLEDGE | RF_INCREMENT)));

#ifdef DEBUGTXN
    debugf("RMAP WRITE START: DEST=%u SRC=%u KEY=%u FLAGS=%x ADDR=0x%02x_%08x LEN=%zu",
           routing->destination.logical_address, routing->source.logical_address, routing->dest_key,
           flags, ext_addr, main_addr, data_length);
#endif
    if (context->monitor->hit_recv_err) {
#ifdef DEBUGTXN
        debug0("RMAP WRITE  STOP: RECVLOOP_STOPPED");
#endif
        return RS_RECVLOOP_STOPPED;
    }

    // use scratch buffer
    uint8_t *out = context->scratch_buffer;
    memset(out, 0, context->scratch_size);
    // and then start writing output bytes according to the write command format
    if (routing->destination.num_path_bytes > 0) {
        assert(routing->destination.num_path_bytes <= RMAP_MAX_PATH);
        assert(routing->destination.path_bytes != NULL);
        memcpy(out, routing->destination.path_bytes, routing->destination.num_path_bytes);
        out += routing->destination.num_path_bytes;
    }
    uint8_t *header_region = out;
    *out++ = routing->destination.logical_address;
    *out++ = PROTOCOL_RMAP;
    int spal = (routing->source.num_path_bytes + 3) / 4;
    assert((spal & RF_SOURCEPATH) == spal);
    uint8_t txn_flags = RF_COMMAND | RF_WRITE | flags | spal;
    *out++ = txn_flags;
    *out++ = routing->dest_key;
    rmap_encode_source_path(&out, &routing->source);
    *out++ = routing->source.logical_address;

    // hold lock to protect pending transaction tracking structures
    mutex_lock(&context->monitor->pending_mutex);
    // guaranteed by contract with caller that only one thread attempts to read or write using an rmap_context_t at a
    // time.
    assert(context->is_pending == false);
    context->pending_txn_id = rmap_next_txn(context->monitor);

    context->is_pending = true;
    context->txn_flags = txn_flags;
    context->read_output = NULL;
    context->read_max_length = 0;
    context->has_received = false;
    context->received_status = -1;
    context->pending_routing = routing;
    context->pending_next = context->monitor->pending_first;
    context->monitor->pending_first = context;
    mutex_unlock(&context->monitor->pending_mutex);

    *out++ = (context->pending_txn_id >> 8) & 0xFF;
    *out++ = (context->pending_txn_id >> 0) & 0xFF;
    *out++ = ext_addr;
    *out++ = (main_addr >> 24) & 0xFF;
    *out++ = (main_addr >> 16) & 0xFF;
    *out++ = (main_addr >> 8) & 0xFF;
    *out++ = (main_addr >> 0) & 0xFF;
    assert(((data_length >> 24) & 0xFF) == 0); // should already be guaranteed by previous checks, but just in case
    *out++ = (data_length >> 16) & 0xFF;
    *out++ = (data_length >> 8) & 0xFF;
    *out++ = (data_length >> 0) & 0xFF;
    // and then compute the header CRC
    uint8_t header_crc = rmap_crc8(header_region, out - header_region);
    *out++ = header_crc;

    // build data body of packet
    assert((out - context->scratch_buffer) + data_length + 1 <= context->scratch_size);
    memcpy(out, data, data_length);
    out += data_length;

    // and data CRC as trailer
    *out++ = rmap_crc8(data, data_length);

    assert(out <= context->scratch_buffer + context->scratch_size);
    // now transmit!
    int wstatus = fakewire_exc_write(context->monitor->exc, context->scratch_buffer, out - context->scratch_buffer);

    // re-acquire the lock and make sure our state is untouched
    mutex_lock(&context->monitor->pending_mutex);
    assert(context->is_pending == true);

    // exactly how we determine the final status depends on whether the network write was successful,
    // and whether we expect a reply from the remote device.
    rmap_status_t status_out;

    if (wstatus < 0) {
        // oops! network error!

        status_out = RS_EXCHANGE_DOWN;

        if (context->has_received) {
            // this should not happen, unless a packet got corrupted somehow and confused for a valid reply, so record
            // it as an error.
            debug0("Impossible RMAP receive state; must have gotten a corrupted packet mixed up with a real one.");
        }
    } else if (flags & RF_ACKNOWLEDGE) {
        // if we transmitted successfully, and need an acknowledgement, then all we've got to do is wait for a reply!

        uint64_t timeout = clock_timestamp_monotonic() + RMAP_TIMEOUT_NS;
        while (context->has_received == false && context->monitor->hit_recv_err == false) {
            uint64_t now = clock_timestamp_monotonic();
            if (now >= timeout) {
                break;
            }
            mutex_unlock(&context->monitor->pending_mutex);
            semaphore_take_timed(&context->on_complete, timeout - now);
            mutex_lock(&context->monitor->pending_mutex);
    
            assert(context->is_pending == true);
        }

        // got a reply!
        if (context->has_received == true) {
            status_out = context->received_status;
        } else if (context->monitor->hit_recv_err == true) {
            status_out = RS_RECVLOOP_STOPPED;
        } else {
            assert(clock_timestamp_monotonic() > timeout);
            status_out = RS_TRANSACTION_TIMEOUT;
        }
    } else {
        // if we transmitted successfully, but didn't ask for a reply, we can just assume success!

        status_out = RS_OK;

        if (context->has_received) {
            // this should not happen, unless a packet got corrupted somehow and confused for a valid reply, so record
            // it as an error.
            debug0("Impossible RMAP receive state; must have gotten a corrupted packet mixed up with a real one.");
        }
    }

    // and now we can remove our pending entry from the linked list, so that the transaction ID can be reused by others
    assert(context->is_pending == true);
    context->is_pending = false;
    rmap_context_t **entry = &context->monitor->pending_first;
    while (*entry != context) {
        assert(*entry != NULL); // context should always be in the linked list somewhere!
        entry = &(*entry)->pending_next;
    }
    *entry = context->pending_next;
    context->pending_next = NULL;
    context->pending_txn_id = 0;
    context->pending_routing = NULL;
    mutex_unlock(&context->monitor->pending_mutex);

#ifdef DEBUGTXN
    debugf("RMAP WRITE  STOP: DEST=%u SRC=%u KEY=%u STATUS=%u",
           routing->destination.logical_address, routing->source.logical_address, routing->dest_key, status_out);
#endif
    return status_out;
}

// Returns status code.
rmap_status_t rmap_read(rmap_context_t *context, rmap_addr_t *routing, rmap_flags_t flags,
                        uint8_t ext_addr, uint32_t main_addr, size_t *data_length, void *data_out) {
    // make sure we didn't get any null pointers
    assert(context != NULL && routing != NULL && data_length != NULL && data_out != NULL);
    // make sure the monitor has enough space to buffer this much data in scratch memory when receiving
    uint32_t max_data_length = *data_length;
    assert(0 < max_data_length && max_data_length <= RMAP_MAX_DATA_LEN && max_data_length + SCRATCH_MARGIN_READ <= context->monitor->scratch_size);
    // make sure flags are valid
    assert(flags == (flags & RF_INCREMENT));

#ifdef DEBUGTXN
    debugf("RMAP  READ START: DEST=%u SRC=%u KEY=%u FLAGS=%x ADDR=0x%02x_%08x MAXLEN=%zu",
           routing->destination.logical_address, routing->source.logical_address, routing->dest_key,
           flags, ext_addr, main_addr, *data_length);
#endif
    if (context->monitor->hit_recv_err) {
#ifdef DEBUGTXN
        debug0("RMAP  READ  STOP: RECVLOOP_STOPPED");
#endif
        return RS_RECVLOOP_STOPPED;
    }

    // use scratch buffer
    uint8_t *out = context->scratch_buffer;
    memset(out, 0, context->scratch_size);
    // and then start writing output bytes according to the read command format
    if (routing->destination.num_path_bytes > 0) {
        assert(routing->destination.num_path_bytes <= RMAP_MAX_PATH);
        assert(routing->destination.path_bytes != NULL);
        memcpy(out, routing->destination.path_bytes, routing->destination.num_path_bytes);
        out += routing->destination.num_path_bytes;
    }
    uint8_t *header_region = out;
    *out++ = routing->destination.logical_address;
    *out++ = PROTOCOL_RMAP;
    int spal = (routing->source.num_path_bytes + 3) / 4;
    assert((spal & RF_SOURCEPATH) == spal);
    uint8_t txn_flags = RF_COMMAND | RF_ACKNOWLEDGE | flags | spal;
    *out++ = txn_flags;
    *out++ = routing->dest_key;
    rmap_encode_source_path(&out, &routing->source);
    *out++ = routing->source.logical_address;

    // hold lock to protect pending transaction tracking structures
    mutex_lock(&context->monitor->pending_mutex);
    // guaranteed by contract with caller that only one thread attempts to read or write using an rmap_context_t at a
    // time.
    assert(context->is_pending == false);
    context->pending_txn_id = rmap_next_txn(context->monitor);

    context->is_pending = true;
    context->txn_flags = txn_flags;
    context->read_output = data_out;
    context->read_max_length = max_data_length;
    context->read_actual_length = 0xFFFFFFFF; // set invalid value
    context->has_received = false;
    context->pending_routing = routing;
    context->pending_next = context->monitor->pending_first;
    context->monitor->pending_first = context;
    mutex_unlock(&context->monitor->pending_mutex);

    *out++ = (context->pending_txn_id >> 8) & 0xFF;
    *out++ = (context->pending_txn_id >> 0) & 0xFF;
    *out++ = ext_addr;
    *out++ = (main_addr >> 24) & 0xFF;
    *out++ = (main_addr >> 16) & 0xFF;
    *out++ = (main_addr >> 8) & 0xFF;
    *out++ = (main_addr >> 0) & 0xFF;
    assert(((max_data_length >> 24) & 0xFF) == 0); // should already be guaranteed by previous checks, but just in case
    *out++ = (max_data_length >> 16) & 0xFF;
    *out++ = (max_data_length >> 8) & 0xFF;
    *out++ = (max_data_length >> 0) & 0xFF;
    // and then compute the header CRC
    uint8_t header_crc = rmap_crc8(header_region, out - header_region);
    *out++ = header_crc;

    assert(out <= context->scratch_buffer + context->scratch_size);
    // now transmit!
    int wstatus = fakewire_exc_write(context->monitor->exc, context->scratch_buffer, out - context->scratch_buffer);

    // re-acquire the lock and make sure our state is untouched
    mutex_lock(&context->monitor->pending_mutex);
    assert(context->is_pending == true);

    // exactly how we determine the final status depends on whether the network write was successful,
    // and whether we expect a reply from the remote device.
    rmap_status_t status_out;

    if (wstatus < 0) {
        // oops! network error!

        status_out = RS_EXCHANGE_DOWN;
        *data_length = 0;

        if (context->has_received) {
            // this should not happen, unless a packet got corrupted somehow and confused for a valid reply, so record
            // it as an error.
            debug0("Impossible RMAP receive state; must have gotten a corrupted packet mixed up with a real one.");
        }
    } else {
        // if we transmitted successfully, then all we've got to do is wait for a reply!

        uint64_t timeout = clock_timestamp_monotonic() + RMAP_TIMEOUT_NS;
        while (context->has_received == false && context->monitor->hit_recv_err == false) {
            uint64_t now = clock_timestamp_monotonic();
            if (now >= timeout) {
                break;
            }
            mutex_unlock(&context->monitor->pending_mutex);
            semaphore_take_timed(&context->on_complete, timeout - now);
            mutex_lock(&context->monitor->pending_mutex);

            assert(context->is_pending == true);
        }

        // got a reply!
        if (context->has_received == true) {
            status_out = context->received_status;
            assert(context->read_actual_length <= RMAP_MAX_DATA_LEN);
            if (context->read_actual_length > max_data_length) {
                if (status_out == RS_OK) {
                    status_out = RS_DATA_TRUNCATED;
                }
                *data_length = max_data_length;
            } else {
                *data_length = context->read_actual_length;
            }
        } else if (context->monitor->hit_recv_err == true) {
            status_out = RS_RECVLOOP_STOPPED;
            *data_length = 0;
        } else {
            assert(clock_timestamp_monotonic() > timeout);
            status_out = RS_TRANSACTION_TIMEOUT;
            *data_length = 0;
        }
    }

    // and now we can remove our pending entry from the linked list, so that the transaction ID can be reused by others
    assert(context->is_pending == true);
    context->is_pending = false;
    rmap_context_t **entry = &context->monitor->pending_first;
    while (*entry != context) {
        assert(*entry != NULL); // context should always be in the linked list somewhere!
        entry = &(*entry)->pending_next;
    }
    *entry = context->pending_next;
    context->pending_next = NULL;
    context->read_output = NULL;
    context->pending_routing = NULL;
    context->pending_txn_id = 0;
    mutex_unlock(&context->monitor->pending_mutex);

#ifdef DEBUGTXN
    debugf("RMAP  READ  STOP: DEST=%u SRC=%u KEY=%u LEN=%zu STATUS=%u",
           routing->destination.logical_address, routing->source.logical_address, routing->dest_key,
           *data_length, status_out);
#endif
    return status_out;
}

static rmap_context_t *rmap_look_up_txn(rmap_monitor_t *mon, uint16_t txn_id) {
    // assumes that mon->pending_mutex is held
    assert(mon != NULL);
    if (txn_id == 0) {
        return NULL;
    }
    for (rmap_context_t *ctx = mon->pending_first; ctx != NULL; ctx = ctx->pending_next) {
        assert(ctx->is_pending == true);
        if (ctx->pending_txn_id == txn_id) {
            return ctx;
        }
    }
    return NULL;
}

static bool rmap_recv_handle(rmap_monitor_t *mon, uint8_t *in, size_t count) {
    if (count < 8 || in[1] != PROTOCOL_RMAP) {
        return false;
    }
    uint8_t flags = in[2];
    if (flags & RF_WRITE) {
        // write reply
 
        // first, check length, CRC, and flags
        if (count != 8 || rmap_crc8(in, 7) != in[7] ||
                RF_ACKNOWLEDGE != (flags & (RF_RESERVED | RF_COMMAND | RF_ACKNOWLEDGE))) {
            return false;
        }

        // now, search for corresponding transaction
        uint16_t txn_id = (in[5] << 8) | in[6];
        mutex_lock(&mon->pending_mutex);
        rmap_context_t *ctx = rmap_look_up_txn(mon, txn_id);
        // now check that we found a matching context
        if (ctx == NULL || ctx->txn_flags != (flags | RF_COMMAND) || ctx->has_received) {
            mutex_unlock(&mon->pending_mutex);
            return false;
        }
        assert(ctx->read_output == NULL);
        // check that routing addresses match
        rmap_addr_t *routing = ctx->pending_routing;
        assert(routing != NULL);
        if (in[0] != routing->source.logical_address || in[4] != routing->destination.logical_address) {
            mutex_unlock(&mon->pending_mutex);
            return false;
        }
        ctx->has_received = true;
        ctx->received_status = in[3];
        mutex_unlock(&mon->pending_mutex);
        semaphore_give(&ctx->on_complete);
        return true;
    } else {
        // read reply

        // first, check length, header CRC, flags, and reserved byte
        if (count < 13 || rmap_crc8(in, 11) != in[11] || in[7] != 0 ||
                RF_ACKNOWLEDGE != (flags & (RF_RESERVED | RF_COMMAND | RF_ACKNOWLEDGE | RF_VERIFY))) {
            return false;
        }

        // second, validate full length and data CRC after parsing data length.
        uint32_t data_length = (in[8] << 16) | (in[9] << 8) | in[10];
        if (count != 13 + data_length || rmap_crc8(&in[12], data_length) != in[count - 1]) {
            return false;
        }

        // now, search for corresponding transaction
        uint16_t txn_id = (in[5] << 8) | in[6];
        mutex_lock(&mon->pending_mutex);
        rmap_context_t *ctx = rmap_look_up_txn(mon, txn_id);
        // now check that we found a matching context
        if (ctx == NULL || ctx->txn_flags != (flags | RF_COMMAND) || ctx->has_received) {
            mutex_unlock(&mon->pending_mutex);
            return false;
        }
        assert(ctx->read_output != NULL);
        // check that routing addresses match
        rmap_addr_t *routing = ctx->pending_routing;
        assert(routing != NULL);
        if (in[0] != routing->source.logical_address || in[4] != routing->destination.logical_address) {
            mutex_unlock(&mon->pending_mutex);
            return false;
        }
        ctx->has_received = true;
        ctx->received_status = in[3];
        ctx->read_actual_length = data_length;
        size_t copy_len = data_length;
        if (copy_len > ctx->read_max_length) {
            copy_len = ctx->read_max_length;
        }
        memcpy(ctx->read_output, &in[12], copy_len);
        mutex_unlock(&mon->pending_mutex);
        semaphore_give(&ctx->on_complete);
        return true;
    }
}

static void *rmap_monitor_recvloop(void *mon_opaque) {
    rmap_monitor_t *mon = (rmap_monitor_t *) mon_opaque;
    assert(mon != NULL && mon->scratch_buffer != NULL);
    assert(mon->hit_recv_err == false);

    for (;;) {
        ssize_t count = fakewire_exc_read(mon->exc, mon->scratch_buffer, mon->scratch_size);
        if (count < 0) {
            mon->hit_recv_err = true;
            return NULL;
        }
        if (count > (ssize_t) mon->scratch_size) {
            debugf("RMAP packet received was too large for buffer: %zd > %zu; discarding.", count, mon->scratch_size);
        } else if (!rmap_recv_handle(mon, mon->scratch_buffer, count)) {
            debug0("RMAP packet received was corrupted or unexpected.");
        }
    }
}
