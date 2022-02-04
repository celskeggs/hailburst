#ifndef FSW_VOCHART_H
#define FSW_VOCHART_H

/*
 * This file contains an implementation of a "voting sticky note chart," based on the existing non-voting sticky note
 * chart implementation provided.
 *
 * This is a crash-safe IPC mechanism for a single replicated client and a single replicated server to communicate by
 * passing back and forth "notes." Each note contains room for both a request and a reply.
 */

#include <synch/chart.h>

#ifndef __PYTHON_PREPROCESS__
#error Python preprocessor is required for vochart code to compile
#endif

typedef struct {
    const size_t note_size;
    const size_t note_count;
    uint8_t * const local_note;

    const uint8_t peer_replicas;
    chart_t * const * const peer_charts;

    enum {
        RECV_NOT_CHECKED,
        RECV_REACHED_MAJORITY,
        RECV_FAILED_MAJORITY,
    } receive_state;
} vochart_peer_t;

typedef struct {
    vochart_peer_t server;
} vochart_server_t;

typedef struct {
    vochart_peer_t client;
} vochart_client_t;

// TODO: do something to verify that settings match across compile units

#define VOCHART_REGISTER(v_ident, v_client_replicas, v_server_replicas, v_note_size, v_note_count)                    \
    static_repeat(v_client_replicas, client_replica_id) {                                                             \
        static_repeat(v_server_replicas, server_replica_id) {                                                         \
            CHART_REGISTER(symbol_join(v_ident, client_replica_id, server_replica_id),                                \
                           v_note_size, v_note_count);                                                                \
        }                                                                                                             \
    }

#define VOCHART_SERVER(v_ident, v_server_id, v_client_replicas, v_note_size, v_note_count, v_notify, v_param)         \
    static_repeat(v_client_replicas, client_replica_id) {                                                             \
        CHART_SERVER_NOTIFY(symbol_join(v_ident, client_replica_id, v_server_id), v_notify, v_param);                 \
    }                                                                                                                 \
    uint8_t v_ident ## _server_ ## v_server_id ## _note[v_note_size];                                                 \
    vochart_server_t v_ident ## _server_ ## v_server_id = {                                                           \
        .server = {                                                                                                   \
            .note_size = (v_note_size),                                                                               \
            .note_count = (v_note_count),                                                                             \
            .local_note = v_ident ## _server_ ## v_server_id ## _note,                                                \
            .peer_replicas = v_client_replicas,                                                                       \
            .peer_charts = (chart_t * const []) {                                                                     \
                static_repeat(v_client_replicas, client_replica_id) {                                                 \
                    &symbol_join(v_ident, client_replica_id, v_server_id),                                            \
                }                                                                                                     \
            },                                                                                                        \
            .reached_majority = false,                                                                                \
        },                                                                                                            \
    }

#define VOCHART_SERVER_PTR(v_ident, v_server_id) (&v_ident ## _server_ ## v_server_id)

#define VOCHART_CLIENT(v_ident, v_client_id, v_server_replicas, v_note_size, v_note_count, v_notify, v_param)         \
    static_repeat(v_server_replicas, server_replica_id) {                                                             \
        CHART_CLIENT_NOTIFY(symbol_join(v_ident, v_client_id, server_replica_id), v_notify, v_param);                 \
    }                                                                                                                 \
    uint8_t v_ident ## _client_ ## v_client_id ## _note[v_note_size];                                                 \
    vochart_client_t v_ident ## _client_ ## v_client_id = {                                                           \
        .client = {                                                                                                   \
            .note_size = (v_note_size),                                                                               \
            .note_count = (v_note_count),                                                                             \
            .local_note = v_ident ## _client_ ## v_client_id ## _note,                                                \
            .peer_replicas = v_server_replicas,                                                                       \
            .peer_charts = (chart_t * const []) {                                                                     \
                static_repeat(v_server_replicas, server_replica_id) {                                                 \
                    &symbol_join(v_ident, v_client_id, server_replica_id),                                            \
                }                                                                                                     \
            },                                                                                                        \
            .reached_majority = false,                                                                                \
        },                                                                                                            \
    }

#define VOCHART_CLIENT_PTR(v_ident, v_server_id) (&v_ident ## _client_ ## v_server_id)

static inline size_t vochart_client_note_size(vochart_client_t *vclient) {
    assert(vclient != NULL);
    return vclient->client.note_size;
}

static inline size_t vochart_server_note_size(vochart_server_t *vserver) {
    assert(vserver != NULL);
    return vserver->server.note_size;
}

static inline size_t vochart_client_note_count(vochart_client_t *vclient) {
    assert(vclient != NULL);
    return vclient->client.note_count;
}

static inline size_t vochart_server_note_count(vochart_server_t *vserver) {
    assert(vserver != NULL);
    return vserver->server.note_count;
}

void *vochart_request_start(vochart_client_t *vclient, bool *compare_ok_out);

chart_index_t vochart_request_avail(vochart_client_t *vclient);

void vochart_request_send(vochart_client_t *vclient);

void *vochart_reply_start(vochart_server_t *vserver, bool *compare_ok_out);

chart_index_t vochart_reply_avail(vochart_server_t *vserver);

void vochart_reply_send(vochart_server_t *vserver);

#endif /* FSW_VOCHART_H */
