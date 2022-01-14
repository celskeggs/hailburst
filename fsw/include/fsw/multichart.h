#ifndef FSW_MULTICHART_H
#define FSW_MULTICHART_H

/*
 * A multichart is a data structure that provides a "multi-client single-server sticky note chart." You can think of it
 * as a generalization of the chart structure. It preserves the strict ordering of the regular structure, despite
 * having multiple queues involved. It remains lockless and restartable.
 */

#include <fsw/chart.h>
#include <fsw/preprocessor.h>

struct multichart_note_header {
    uint64_t insertion_timestamp;
};

// immutable configuration (after setup)
typedef struct multichart_client_st {
    chart_t *chart;
    struct multichart_client_st *next_client;
} multichart_client_t;

// immutable configuration (after setup)
typedef struct multichart_server_st {
    void (*notify_server)(void *);
    void *notify_server_param;

    size_t note_size;

    multichart_client_t *first_client;
} multichart_server_t;

#define MULTICHART_SERVER_REGISTER(s_ident, s_note_size, s_notify_fn, s_notify_param) \
    static_assert(s_note_size > 0, "must have positive note size");                   \
    multichart_server_t s_ident = {                                                   \
        .notify_server = PP_ERASE_TYPE(s_notify_fn, s_notify_param),                  \
        .notify_server_param = (void *) (s_notify_param),                             \
        .note_size = (s_note_size),                                                   \
        .first_client = NULL, /* will be filled in later */                           \
    }

#define MULTICHART_CLIENT_REGISTER(c_ident, s_ident, s_note_size, c_note_count, c_notify_fn, c_notify_param) \
    extern multichart_server_t s_ident;                                                                      \
    static_assert(s_note_size > 0, "must have positive note size");                                          \
    static_assert(c_note_count > 0, "must have positive number of notes");                                   \
    CHART_REGISTER(c_ident ## _chart, s_note_size + sizeof(struct multichart_note_header), (c_note_count));  \
    CHART_CLIENT_NOTIFY(c_ident ## _chart, c_notify_fn, c_notify_param);                                     \
    multichart_client_t c_ident = {                                                                          \
        .chart = &c_ident ## _chart,                                                                         \
        /* next_client populated during init */                                                              \
    };                                                                                                       \
    static void c_ident ## _init(void) {                                                                     \
        assert(s_note_size == s_ident.note_size);                                                            \
        /* TODO: is this the best function to attach? or would it be better to notify indirectly? */         \
        chart_attach_server(&c_ident ## _chart, s_ident.notify_server, s_ident.notify_server_param);         \
        c_ident.next_client = s_ident.first_client;                                                          \
        s_ident.first_client = &c_ident;                                                                     \
    }                                                                                                        \
    PROGRAM_INIT(STAGE_RAW, c_ident ## _init)

static inline size_t multichart_server_note_size(multichart_server_t *server) {
    assert(server != NULL);
    return server->note_size;
}

static inline size_t multichart_client_note_size(multichart_client_t *client) {
    assert(client != NULL && client->chart != NULL);
    return chart_note_size(client->chart) - sizeof(struct multichart_note_header);
}

static inline chart_index_t multichart_client_note_count(multichart_client_t *client) {
    assert(client != NULL && client->chart != NULL);
    return chart_note_count(client->chart);
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
