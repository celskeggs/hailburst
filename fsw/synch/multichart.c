#include <string.h>

#include <hal/atomic.h>
#include <hal/debug.h>
#include <hal/timer.h>
#include <synch/multichart.h>

void *multichart_request_start(multichart_client_t *client) {
    assert(client != NULL);
    struct multichart_note_header *header = chart_request_start(client->chart);
    if (header == NULL) {
        return NULL;
    }
    // return space following the header
    return header + 1;
}

void multichart_request_send(multichart_client_t *client, void *note) {
    assert(client != NULL && note != NULL);
    struct multichart_note_header *header = chart_request_start(client->chart);
    assert(header != NULL && header + 1 == note);
    // TODO: figure out a solution to the timestamp order <-> chart order race condition
    header->insertion_timestamp = timer_now_ns();
    chart_request_send(client->chart, 1);
}

void *multichart_reply_start(multichart_server_t *server, uint64_t *timestamp_out) {
    assert(server != NULL);
    struct multichart_note_header *min_header = NULL;
    for (multichart_client_t *client = atomic_load(server->first_client); client != NULL; client = client->next_client) {
        struct multichart_note_header *client_next = chart_reply_start(client->chart);
        if (client_next != NULL
                && (min_header == NULL || client_next->insertion_timestamp < min_header->insertion_timestamp)) {
            min_header = client_next;
        }
    }
    if (min_header == NULL) {
        return NULL;
    }
    if (timestamp_out != NULL) {
        *timestamp_out = min_header->insertion_timestamp;
    }
    return min_header + 1;
}

// confirm and send one or more replies, which will be in the first notes available.
void multichart_reply_send(multichart_server_t *server, void *note) {
    assert(server != NULL && note != NULL);
    for (multichart_client_t *client = atomic_load(server->first_client); client != NULL; client = client->next_client) {
        struct multichart_note_header *client_next = chart_reply_start(client->chart);
        if (client_next != NULL && client_next + 1 == note) {
            chart_reply_send(client->chart, 1);
            return;
        }
    }
    abortf("attempt to send reply that cannot be found");
}
