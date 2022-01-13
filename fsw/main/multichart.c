#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <hal/atomic.h>
#include <fsw/clock.h>
#include <fsw/debug.h>
#include <fsw/multichart.h>

struct note_header {
    uint64_t insertion_timestamp;
};

void multichart_init_server(multichart_server_t *server, size_t note_size,
                            void (*notify_server)(void *), void *param) {
    assert(server != NULL && notify_server != NULL && note_size > 0);
    server->notify_server = notify_server;
    server->notify_server_param = param;
    server->note_size = note_size;
    server->first_client = NULL;
}

void multichart_init_client(multichart_client_t *client, multichart_server_t *server, chart_index_t note_count,
                            void (*notify_client)(void *), void *param) {
    assert(client != NULL && server != NULL && note_count > 0);
    client->server = server;
    chart_init(&client->chart, server->note_size + sizeof(struct note_header), note_count);
    chart_attach_client(&client->chart, notify_client, param);
    // TODO: is this the best function to attach?
    chart_attach_server(&client->chart, server->notify_server, server->notify_server_param);
    client->next_client = server->first_client;
    atomic_store(server->first_client, client);
}

void *multichart_request_start(multichart_client_t *client) {
    assert(client != NULL);
    struct note_header *header = chart_request_start(&client->chart);
    if (header == NULL) {
        return NULL;
    }
    // return space following the header
    return header + 1;
}

void multichart_request_send(multichart_client_t *client, void *note) {
    assert(client != NULL && note != NULL);
    struct note_header *header = chart_request_start(&client->chart);
    assert(header != NULL && header + 1 == note);
    // TODO: figure out a solution to the timestamp order <-> chart order race condition
    header->insertion_timestamp = clock_timestamp_monotonic();
    chart_request_send(&client->chart, 1);
}

void *multichart_reply_start(multichart_server_t *server, uint64_t *timestamp_out) {
    assert(server != NULL);
    struct note_header *min_header = NULL;
    for (multichart_client_t *client = atomic_load(server->first_client); client != NULL; client = client->next_client) {
        struct note_header *client_next = chart_reply_start(&client->chart);
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
        struct note_header *client_next = chart_reply_start(&client->chart);
        if (client_next != NULL && client_next + 1 == note) {
            chart_reply_send(&client->chart, 1);
            return;
        }
    }
    abortf("attempt to send reply that cannot be found");
}
