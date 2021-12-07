#ifndef FSW_MULTICHART_H
#define FSW_MULTICHART_H

/*
 * A multichart is a data structure that provides a "multi-client single-server sticky note chart." You can think of it
 * as a generalization of the chart structure. It preserves the strict ordering of the regular structure, despite
 * having multiple queues involved. It remains lockless and restartable.
 */

#include <fsw/chart.h>

// immutable configuration (after setup)
typedef struct multichart_client_st {
    chart_t chart;
    struct multichart_server_st *server;
    struct multichart_client_st *next_client;
} multichart_client_t;

// immutable configuration (after setup)
typedef struct multichart_server_st {
    void (*notify_server)(void *);
    void *notify_server_param;

    size_t note_size;

    multichart_client_t *first_client;
} multichart_server_t;

void multichart_init_server(multichart_server_t *server, size_t note_size,
                            void (*notify_server)(void *), void *param);
void multichart_init_client(multichart_client_t *client, multichart_server_t *server, chart_index_t note_count,
                            void (*notify_client)(void *), void *param);

static inline size_t multichart_server_note_size(multichart_server_t *server) {
    assert(server != NULL);
    return server->note_size;
}

static inline size_t multichart_client_note_size(multichart_client_t *client) {
    assert(client != NULL && client->server != NULL);
    return client->server->note_size;
}

static inline chart_index_t multichart_client_note_count(multichart_client_t *client) {
    assert(client != NULL);
    return chart_note_count(&client->chart);
}

// if a request can be sent on any note, return a pointer to the note's memory, otherwise NULL.
// if called multiple times, will return the same note.
void *multichart_request_start(multichart_client_t *client);
// confirm and send the next request.
void multichart_request_send(multichart_client_t *client, void *note);

// if a request has been received on any note (and therefore a reply can be written), return a pointer to that note's
// memory, otherwise NULL. if called multiple times, will return the same note.
// if timestamp_out is not NULL, it will be populated with the clock_timestamp() value at which the request was added
// to the multichart.
void *multichart_reply_start(multichart_server_t *server, uint64_t *timestamp_out);
// confirm and send one or more replies, which will be in the first notes available.
void multichart_reply_send(multichart_server_t *server, void *note);

#endif /* FSW_MULTICHART_H */
