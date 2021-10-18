#ifndef FSW_CHART_H
#define FSW_CHART_H

/*
 * This file contains an implementation of a "sticky note chart" data structure.
 * This is a crash-safe IPC mechanism, where the CLIENT writes REQUESTS onto NOTES in a fixed-size array.
 * Each request remains on its note until the SERVER processes it and writes a REPLY back to the note.
 * Then, the client can handle the reply and ACKNOWLEDGE it, which frees the note for reuse.
 *
 * Charts are single-client single-server. At least for now.
 */

#include <stdint.h>
#include <hal/thread.h>

typedef enum {
    CHART_NOTE_BLANK = 0,
    CHART_NOTE_REQUEST,
    CHART_NOTE_REPLY,

    CHART_NUM_STATES,
} chart_note_state_t;

typedef uint32_t chart_index_t;

typedef struct {
    critical_t          critical_section;

    void (*notify_server)(void *);
    void (*notify_client)(void *);
    void *notify_param;

    size_t              note_size;
    chart_index_t       note_count;
    uint8_t            *note_storage; // note_count elements of note_size each
    chart_note_state_t *note_info;    // note_count elements

    chart_index_t next_blank;
    chart_index_t next_request;
    chart_index_t next_reply;
} chart_t;

// initializes a chart. notify_server and notify_client should be fast and non-blocking procedures that let the
// appropriate party to the chart know to check it again.
void chart_init(chart_t *chart, size_t note_size, chart_index_t note_count, void (*notify_server)(void *), void (*notify_client)(void *), void *param);
void chart_destroy(chart_t *chart);

// requests are sent by the CLIENT

// if any note is blank, return a pointer to its memory, otherwise NULL.
// if called multiple times, will return the same note.
void *chart_request_start(chart_t *chart);
// write back a request.
void chart_request_send(chart_t *chart, void *note);

// replies are sent by the SERVER

// if any unhandled requests are available, return a pointer to the memory of one of them, otherwise NULL.
// if called multiple times, will return the same note.
void *chart_reply_start(chart_t *chart);
// write back a reply to the specified note.
void chart_reply_send(chart_t *chart, void *note);

// acknowledgements are sent by the CLIENT

// if any unacknowledged requests are available, return a pointer to its memory, otherwise NULL.
// if called multiple times, will return the same note.
void *chart_ack_start(chart_t *chart);
// write back an acknowledgement.
void chart_ack_send(chart_t *chart, void *note);

#endif /* FSW_CHART_H */
