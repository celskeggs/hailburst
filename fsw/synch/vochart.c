#include <string.h>

#include <synch/vochart.h>

// TODO: figure out what to do in the case of disagreements

// Out of the 'replicas' memory regions of 'length' in 'inputs', find the majority value for the region, if any.
// Treat any regions set to NULL as not matching any other region.
// If found: copy the majority value of the region into 'output' and return true.
// Otherwise: possibly set 'output' to arbitrary data and return false.
static bool vote_memory(uint8_t replicas, void **inputs, size_t length, void *output) {
    /* arbitrary limit to make it easier to think about overflow errors */
    assert(replicas >= 1 && replicas <= 101);
    assert(inputs != NULL);
    assert(length >= 1);
    assert(output != NULL);

    uint8_t majority = (1 + replicas) / 2;
    for (uint8_t baseline = 0; baseline <= replicas - majority; baseline++) {
        if (inputs[baseline] == NULL) {
            continue;
        }
        memcpy(output, inputs[baseline], length);
        uint8_t matches = 1;
        for (uint8_t compare = baseline + 1; compare < replicas; compare++) {
            if (inputs[compare] == NULL) {
                continue;
            }
            if (memcmp(output, inputs[compare], length) == 0) {
                matches++;
                break;
            }
        }
        if (matches >= majority) {
            return true;
        }
    }
    return false;
}

static inline void *vochart_generic_start(vochart_peer_t *vpeer, void *(*start_impl)(chart_t *)) {
    assert(vpeer != NULL);
    if (vpeer->reached_majority) {
        return vpeer->local_note;
    }
    uint8_t peers = vpeer->peer_replicas;
    void *inputs[peers];
    for (uint8_t i = 0; i < vpeer->peer_replicas; i++) {
        inputs[i] = start_impl(vpeer->peer_charts[i]);
    }
    if (!vote_memory(peers, inputs, vpeer->note_size, vpeer->local_note)) {
        // either a majority of the peer queues were full, which means we definitely can't write, or there was no
        // majority of peer queues with the same data in the next exposed note, which means we have a mismatch.
        // TODO: figure out how to get out of that mismatch situation.
        return NULL;
    }
    vpeer->reached_majority = true;
    // we've populated the local note with the consensus data, and now we can let the client code edit it.
    return vpeer->local_note;
}

static inline void vochart_generic_send(vochart_peer_t *vpeer, void *(*start_impl)(chart_t *),
                                        void (*send_impl)(chart_t *, chart_index_t)) {
    assert(vpeer->reached_majority == true);
    for (uint8_t i = 0; i < vpeer->peer_replicas; i++) {
        void *note = start_impl(vpeer->peer_charts[i]);
        if (note != NULL) {
            memcpy(note, vpeer->local_note, vpeer->note_size);
            send_impl(vpeer->peer_charts[i], 1);
        }
    }
}

void *vochart_request_start(vochart_client_t *vclient) {
    return vochart_generic_start(&vclient->client, chart_request_start);
}

void vochart_request_send(vochart_client_t *vclient) {
    vochart_generic_send(&vclient->client, chart_request_start, chart_request_send);
}

void *vochart_reply_start(vochart_server_t *vserver) {
    return vochart_generic_start(&vserver->server, chart_reply_start);
}

void vochart_reply_send(vochart_server_t *vserver) {
    vochart_generic_send(&vserver->server, chart_reply_start, chart_reply_send);
}
