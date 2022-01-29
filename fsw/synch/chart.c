#include <string.h>

#include <hal/atomic.h>
#include <hal/debug.h>
#include <synch/chart.h>

void chart_attach_server(chart_t *chart, void (*notify_server)(void *), void *param) {
    assert(chart != NULL && notify_server != NULL);
    assert(chart->notify_server == NULL && chart->notify_server_param == NULL);
    chart->notify_server = notify_server;
    chart->notify_server_param = param;
}

void chart_attach_client(chart_t *chart, void (*notify_client)(void *), void *param) {
    assert(chart != NULL && notify_client != NULL);
    assert(chart->notify_client == NULL && chart->notify_client_param == NULL);
    chart->notify_client = notify_client;
    chart->notify_client_param = param;
}

// if a request can be sent on any note, return a pointer to the note's memory, otherwise NULL.
// if called multiple times, will return the same note.
void *chart_request_start(chart_t *chart) {
    assert(chart != NULL);
    if (chart_request_avail(chart) > 0) {
        return chart_request_peek(chart, 0);
    } else {
        return NULL;
    }
}

// confirm and send one or more requests, which will be in the first notes available.
void chart_request_send(chart_t *chart, chart_index_t count) {
    assert(chart != NULL);
    chart_index_t avail = chart_request_avail(chart);
    assertf(1 <= count && count <= avail, "count=%u, avail=%u", count, avail);
    // TODO: can this be relaxed, since it's limited to a single processor?
    atomic_store(chart->request_ptr, chart->request_ptr + count);

    assert(chart->notify_server != NULL); // no server registered???
    chart->notify_server(chart->notify_server_param);
}

// count the number of notes currently available for sending requests.
chart_index_t chart_request_avail(chart_t *chart) {
    assert(chart != NULL);
    // request leads, reply lags
    uint32_t ahead = (chart->request_ptr - atomic_load(chart->reply_ptr) + 2 * chart->note_count)
                        % (2 * chart->note_count);
    assertf(ahead <= chart->note_count, "ahead=%u, note_count=%u", ahead, chart->note_count);
    return chart->note_count - ahead;
}

// grab the nth note available for sending requests. newly available notes are always added to the end.
// asserts if index is invalid.
void *chart_request_peek(chart_t *chart, chart_index_t offset) {
    assert(chart != NULL);
    chart_index_t avail = chart_request_avail(chart);
    assertf(offset < avail, "offset=%u, avail=%u", offset, avail);
    return chart_get_note(chart, (chart->request_ptr + offset) % chart->note_count);
}

// if a request has been received on any note (and therefore a reply can be written), return a pointer to that note's
// memory, otherwise NULL. if called multiple times, will return the same note.
void *chart_reply_start(chart_t *chart) {
    assert(chart != NULL);
    if (chart_reply_avail(chart) > 0) {
        return chart_reply_peek(chart, 0);
    } else {
        return NULL;
    }
}

// confirm and send one or more replies, which will be in the first notes available.
void chart_reply_send(chart_t *chart, chart_index_t count) {
    assert(chart != NULL);
    chart_index_t avail = chart_reply_avail(chart);
    assertf(1 <= count && count <= avail, "self=%p, count=%u, avail=%u", chart, count, avail);
    // TODO: can this be relaxed, since it's limited to a single processor?
    atomic_store(chart->reply_ptr, chart->reply_ptr + count);

    assert(chart->notify_client != NULL); // no client registered???
    chart->notify_client(chart->notify_client_param);
}

// count the number of notes with requests currently available for replies.
chart_index_t chart_reply_avail(chart_t *chart) {
    assert(chart != NULL);
    // request leads, reply lags
    uint32_t ahead = (atomic_load(chart->request_ptr) - chart->reply_ptr + 2 * chart->note_count)
                        % (2 * chart->note_count);
    assertf(ahead <= chart->note_count, "ahead=%u, note_count=%u", ahead, chart->note_count);
    return ahead;
}

// grab the nth note with a pending request. newly available notes are always added to the end.
// asserts if index is invalid.
void *chart_reply_peek(chart_t *chart, chart_index_t offset) {
    assert(chart != NULL);
    chart_index_t avail = chart_reply_avail(chart);
    assertf(offset < avail, "offset=%u, avail=%u", offset, avail);
    return chart_get_note(chart, (chart->reply_ptr + offset) % chart->note_count);
}
