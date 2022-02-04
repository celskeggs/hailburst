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

static inline void *vochart_generic_start(vochart_peer_t *vpeer, bool *compare_ok_out, void *(*start_impl)(chart_t*)) {
    assert(vpeer != NULL);
    if (vpeer->receive_state != RECV_NOT_CHECKED) {
        if (compare_ok_out) {
            *compare_ok_out = (vpeer->receive_state == RECV_REACHED_MAJORITY);
        }
        return vpeer->local_note;
    }
    uint8_t peers = vpeer->peer_replicas;
    void *inputs[peers];
    for (uint8_t i = 0; i < peers; i++) {
        assert(chart_note_size(vpeer->peer_charts[i]) == vpeer->note_size);
        inputs[i] = start_impl(vpeer->peer_charts[i]);
    }
    bool compare_ok = false;
    if (vote_memory(peers, inputs, vpeer->note_size, vpeer->local_note, &compare_ok)) {
        assert(compare_ok == true);
        if (compare_ok_out) {
            *compare_ok_out = true;
        }
        vpeer->receive_state = RECV_REACHED_MAJORITY;
        // we've populated the local note with the consensus data, and now we can let the client code edit it.
        return vpeer->local_note;
    } else if (compare_ok) {
        // nothing is available on the majority of the peer queues
        // TODO: think about what happens if we break a promise made by 'avail' in this case
        if (compare_ok_out) {
            *compare_ok_out = true;
        }
        return NULL;
    } else {
        // TODO: think about whether this can happen just because of a single failure plus a timing differential
        //       between the remaining tasks. Could we split 1-1-1 (wrong-correct-notready)?
        if (compare_ok_out) {
            *compare_ok_out = false;
        }
        vpeer->receive_state = RECV_FAILED_MAJORITY;
        // the peers could not agree on their state. but we must proceed anyway, so that we can recover.
        // make sure to wipe the note so that there is no pretending it has real data.
        memset(vpeer->local_note, 0, vpeer->note_size);
        return vpeer->local_note;
    }
}

static void sort_ascending(chart_index_t *indexes, uint8_t count) {
    /* arbitrary limit to make it easier to think about overflow errors */
    assert(count >= 1 && count <= 101);
    // because count is small, it's actually probably best to do insertion sort.
    for (uint8_t i = 1; i < count; i++) {
        chart_index_t value = indexes[i];
        // decide where it should go
        uint8_t desired_location = 0;
        while (desired_location < i && indexes[desired_location] < value) {
            desired_location++;
        }
        // move elements aside
        for (uint8_t j = i; j > desired_location; j--) {
            indexes[j] = indexes[j - 1];
        }
        // place back inserted element
        indexes[desired_location] = value;
    }
    // the list SHOULD be sorted, but let's make sure
    for (uint8_t i = 1; i < count; i++) {
        assert(indexes[i-1] < indexes[i]);
    }
}

static inline chart_index_t vochart_generic_avail(vochart_peer_t *vpeer, chart_index_t (*avail_impl)(chart_t *)) {
    assert(vpeer != NULL);
    uint8_t peers = vpeer->peer_replicas;
    chart_index_t avails[peers];
    for (uint8_t i = 0; i < peers; i++) {
        avails[i] = avail_impl(vpeers->peer_charts[i]);
    }
    sort_ascending(avails, peers);
    // TODO: validate that this is correct somehow
    return avails[(1 + peers) / 2 - 1];
}

static inline void vochart_generic_send(vochart_peer_t *vpeer, void *(*start_impl)(chart_t *),
                                        void (*send_impl)(chart_t *, chart_index_t)) {
    assert(vpeer->receive_state == RECV_REACHED_MAJORITY || vpeer->receive_state == RECV_FAILED_MAJORITY);
    for (uint8_t i = 0; i < vpeer->peer_replicas; i++) {
        void *note = start_impl(vpeer->peer_charts[i]);
        if (note != NULL) {
            memcpy(note, vpeer->local_note, vpeer->note_size);
            send_impl(vpeer->peer_charts[i], 1);
        }
    }
}

void *vochart_request_start(vochart_client_t *vclient, bool *compare_ok_out) {
    return vochart_generic_start(&vclient->client, compare_ok_out, chart_request_start);
}

chart_index_t vochart_request_avail(vochart_client_t *vclient) {
    return vochart_generic_avail(&vclient->client, chart_request_avail);
}

void vochart_request_send(vochart_client_t *vclient) {
    vochart_generic_send(&vclient->client, chart_request_start, chart_request_send);
}

void *vochart_reply_start(vochart_server_t *vserver, bool *compare_ok_out) {
    return vochart_generic_start(&vserver->server, compare_ok_out, chart_reply_start);
}

chart_index_t vochart_reply_avail(vochart_server_t *vserver) {
    return vochart_generic_avail(&vclient->client, chart_reply_avail);
}

void vochart_reply_send(vochart_server_t *vserver) {
    vochart_generic_send(&vserver->server, chart_reply_start, chart_reply_send);
}
