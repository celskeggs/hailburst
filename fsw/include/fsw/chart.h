#ifndef FSW_CHART_H
#define FSW_CHART_H

/*
 * This file contains an implementation of a "sticky note chart" data structure.
 * This is a crash-safe IPC mechanism for a single client and a single server to communicate by passing back and forth
 * "notes." Each note contains room for both a request and a reply.
 *
 * Requests and replies are split into four phases:
 *   1. Peek at state to find the next available notes
 *   2. Read any desired data out of one or more of those notes
 *   3. Write any desired data into the locally-provided side of one or more of those notes
 *      (Client may write requests, server may write replies; one side MUST NOT clear or modify the other side's data)
 *   4. Transmit the first N notes to the other side of the connection. (Without any copying, of course.)
 *
 * The client can always peek at its previous requests, and the server can always peek at its previous replies, because
 * in both cases they have full authority to read the things they have written -- EVEN WHEN they do not actually have
 * "ownership" of the note in question.
 *
 * This implementation is, by itself, completely lockless, but it depends on a user-provided notification mechanism;
 * that mechanism might not be lockless.
 */

#include <stdint.h>
#include <hal/thread.h>

typedef uint32_t chart_index_t;

typedef struct {
    // immutable configuration (after setup)
    void (*notify_server)(void *);
    void *notify_server_param;
    void (*notify_client)(void *);
    void *notify_client_param;

    // immutable state
    size_t              note_size;
    chart_index_t       note_count;
    uint8_t            *note_storage; // note_count elements of note_size each

    // mutable state
    chart_index_t request_ptr; // writable only by client, wraps at 2 * note_count
    chart_index_t reply_ptr;   // writable only by server, wraps at 2 * note_count
} chart_t;

// initializes a chart. notify_server and notify_client should be fast and non-blocking procedures that let the
// appropriate party to the chart know to check it again.
void chart_init(chart_t *chart, size_t note_size, chart_index_t note_count);
void chart_attach_server(chart_t *chart, void (*notify_server)(void *), void *param);
void chart_attach_client(chart_t *chart, void (*notify_client)(void *), void *param);
void chart_destroy(chart_t *chart);

static inline chart_index_t chart_note_size(chart_t *chart) {
    assert(chart != NULL);
    return chart->note_size;
}

static inline chart_index_t chart_note_count(chart_t *chart) {
    assert(chart != NULL);
    return chart->note_count;
}

static inline void *chart_get_note(chart_t *chart, chart_index_t index) {
    assert(chart != NULL && index < chart->note_count);
    return &chart->note_storage[chart->note_size * index];
}

// requests are sent by the CLIENT

// if a request can be sent on any note, return a pointer to the note's memory, otherwise NULL.
// if called multiple times, will return the same note.
void *chart_request_start(chart_t *chart);
// confirm and send one or more requests, which will be in the first notes available.
void chart_request_send(chart_t *chart, chart_index_t count);
// count the number of notes currently available for sending requests.
chart_index_t chart_request_avail(chart_t *chart);
// grab the nth note available for sending requests. newly available notes are always added to the end.
// asserts if index is invalid.
void *chart_request_peek(chart_t *chart, chart_index_t offset);

// replies are sent by the SERVER

// if a request has been received on any note (and therefore a reply can be written), return a pointer to that note's
// memory, otherwise NULL. if called multiple times, will return the same note.
void *chart_reply_start(chart_t *chart);
// confirm and send one or more replies, which will be in the first notes available.
void chart_reply_send(chart_t *chart, chart_index_t count);
// count the number of notes with requests currently available for replies.
chart_index_t chart_reply_avail(chart_t *chart);
// grab the nth note with a pending request. newly available notes are always added to the end.
// asserts if index is invalid.
void *chart_reply_peek(chart_t *chart, chart_index_t offset);

#endif /* FSW_CHART_H */
