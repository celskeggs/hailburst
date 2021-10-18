#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <fsw/chart.h>
#include <hal/thread.h>

void chart_init(chart_t *chart, size_t note_size, chart_index_t note_count, void (*notify_server)(void), void (*notify_client)(void)) {
    assert(chart != NULL && note_size > 0 && note_count > 0 && notify_server != NULL && notify_client != NULL);
    critical_init(&chart->critical_section);

    chart->notify_server = notify_server;
    chart->notify_client = notify_client;

    chart->note_size = note_size;
    chart->note_count = note_count;
    chart->note_storage = (uint8_t *) malloc(note_count * note_size);
    assert(chart->note_storage != NULL);
    chart->note_info = (chart_note_state_t *) malloc(note_count * sizeof(chart_note_state_t));
    assert(chart->note_info != NULL);

    for (chart_index_t i = 0; i < note_count; i++) {
        chart->note_info[i] = CHART_NOTE_BLANK;
    }

    chart->next_blank = chart->next_reply = chart->next_request = 0;
}

static inline void chart_validate_state(chart_note_state_t state) {
    assert(state == CHART_NOTE_BLANK || state == CHART_NOTE_REQUEST || state == CHART_NOTE_REPLY);
}

static inline void chart_consistency_check(chart_t *chart) {
    assert(chart->next_blank < chart->note_count
              && chart->next_request < chart->note_count
              && chart->next_reply < chart->note_count);
}

static inline void *chart_get_note(chart_t *chart, chart_index_t index) {
    assert(index < chart->note_count);
    return &chart->note_storage[chart->note_size * index];
}

static void *chart_any_start(chart_t *chart, chart_note_state_t expected_state, chart_index_t next) {
    chart_validate_state(expected_state);

    critical_enter(&chart->critical_section);
    chart_consistency_check(chart);
    chart_note_state_t state = chart->note_info[next];

    // validate local coherence of structure
    assert((state == CHART_NOTE_BLANK   && next == chart->next_blank)
        || (state == CHART_NOTE_REQUEST && next == chart->next_request)
        || (state == CHART_NOTE_REPLY   && next == chart->next_reply));

    void *note = (state == expected_state ? chart_get_note(chart, chart->next_blank) : NULL);

    critical_exit(&chart->critical_section);
    return note;
}

static void chart_any_send(chart_t *chart, void *note, chart_note_state_t expected_state, chart_note_state_t next_state, chart_index_t *next) {
    critical_enter(&chart->critical_section);
    chart_consistency_check(chart);
    assert(chart->note_info[*next] == expected_state);
    assert(note == chart_get_note(chart, *next));

    chart->note_info[*next] = next_state;
    *next += 1;

    critical_exit(&chart->critical_section);
}

// if any note is blank, return a pointer to its memory, otherwise NULL.
void *chart_request_start(chart_t *chart) {
    return chart_any_start(chart, CHART_NOTE_BLANK, chart->next_blank);
}

// write back a request.
void chart_request_send(chart_t *chart, void *note) {
    chart_any_send(chart, note, CHART_NOTE_BLANK, CHART_NOTE_REQUEST, &chart->next_blank);

    chart->notify_server();
}

// if any unhandled requests are available, return a pointer to the memory of one of them, otherwise NULL.
void *chart_reply_start(chart_t *chart) {
    return chart_any_start(chart, CHART_NOTE_REQUEST, chart->next_request);
}

// write back a reply.
void chart_reply_send(chart_t *chart, void *note) {
    chart_any_send(chart, note, CHART_NOTE_REQUEST, CHART_NOTE_REPLY, &chart->next_blank);

    chart->notify_client();
}

// acknowledgements are sent by the CLIENT

// if any unacknowledged requests are available, return a pointer to its memory, otherwise NULL.
void *chart_ack_start(chart_t *chart) {
    return chart_any_start(chart, CHART_NOTE_REPLY, chart->next_reply);
}

// write back an acknowledgement.
void chart_ack_send(chart_t *chart, void *note) {
    chart_any_send(chart, note, CHART_NOTE_REPLY, CHART_NOTE_BLANK, &chart->next_blank);

    chart->notify_server();
}
