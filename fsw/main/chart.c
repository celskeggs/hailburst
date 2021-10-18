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
    chart->note_info = (chart_note_t *) malloc(note_count * sizeof(chart_note_t));
    assert(chart->note_info != NULL);

    for (chart_note_state_t state = 0; state < CHART_NUM_STATES; state++) {
        chart_index_t index = note_count + state;
        chart_note_t *info = &chart->note_info[index];
        info->state = state;
        info->next_idx = index;
        info->prev_idx = index;
    }
    for (chart_index_t i = 0; i < note_count; i++) {
        chart_note_t *info = &chart->note_info[i];
        info->state = CHART_NOTE_BLANK;
        info->prev_idx = (i == 0 ? note_count + CHART_NOTE_BLANK : i - 1);
        info->next_idx = (i == note_count - 1 ? note_count + CHART_NOTE_BLANK : i + 1);
    }

    chart->nums[CHART_NOTE_BLANK] = note_count;
    chart->nums[CHART_NOTE_REQUEST] = 0;
    chart->nums[CHART_NOTE_REPLY] = 0;
}

static inline void chart_validate_state(chart_note_state_t state) {
    assert(state == CHART_NOTE_BLANK || state == CHART_NOTE_REQUEST || state == CHART_NOTE_REPLY);
}

static inline void chart_consistency_check(chart_t *chart) {
    assert(chart->nums[CHART_NOTE_BLANK] + chart->nums[CHART_NOTE_REQUEST] + chart->nums[CHART_NOTE_REPLY] == chart->note_count);
}

static inline chart_note_t *chart_get_note(chart_t *chart, chart_index_t index, chart_note_state_t state) {
    assert(index < chart->note_count + CHART_NUM_STATES);
    chart_note_t *note = &chart->note_info[index];
    assert(note->state == state);
    return note;
}

static inline chart_index_t chart_get_sentinel_index(chart_t *chart, chart_note_state_t state) {
    chart_validate_state(state);
    return chart->note_count + state;
}

static inline chart_note_t *chart_get_sentinel(chart_t *chart, chart_note_state_t state) {
    return chart_get_note(chart, chart_get_sentinel_index(chart, state), state);
}

static inline void *chart_note_ptr(chart_t *chart, chart_index_t index) {
    assert(index < chart->note_count);
    return &chart->note_storage[chart->note_size * index];
}

static inline chart_index_t chart_note_index(chart_t *chart, void *note) {
    size_t offset = (uint8_t *) note - chart->note_storage;
    chart_index_t index = offset / chart->note_size;
    assert(index * chart->note_size == offset);
    assert(index < chart->note_count);
    return index;
}

static void *chart_next(chart_t *chart, chart_note_state_t search_state) {
    void *note_ptr;

    critical_enter(&chart->critical_section);

    chart_consistency_check(chart);
    chart_note_t *note = chart_get_sentinel(chart, search_state);
    if (chart->nums[search_state] > 0) {
        // make sure there actually is another element
        assert(note != chart_get_note(chart, note->next_idx, search_state));
        note_ptr = chart_note_ptr(chart, note->next_idx);
    } else {
        // make sure there actually isn't another element
        assert(note == chart_get_note(chart, note->next_idx, search_state));
        note_ptr = NULL;
    }

    critical_exit(&chart->critical_section);

    return note_ptr;
}

static void chart_update(chart_t *chart, void *existing, chart_note_state_t old_state, chart_note_state_t new_state) {
    critical_enter(&chart->critical_section);

    chart_consistency_check(chart);
    chart_index_t old_state_sentinel_index = chart_get_sentinel_index(chart, old_state);
    chart_note_t *old_state_sentinel = chart_get_note(chart, old_state_sentinel_index, old_state);
    chart_index_t new_state_sentinel_index = chart_get_sentinel_index(chart, new_state);
    chart_note_t *new_state_sentinel = chart_get_note(chart, new_state_sentinel_index, new_state);
    chart_index_t index = chart_note_index(chart, existing);
    chart_note_t *note_info = chart_get_note(chart, index, old_state);
    // check that things are actually linked as expected
    assert(old_state_sentinel->next_idx == index);
    assert(old_state_sentinel == chart_get_note(chart, note_info->prev_idx, old_state));
    assert(chart->nums[old_state] > 0);

    // update counts
    chart->nums[old_state]--;
    chart->nums[new_state]++;
    // remove from old linked list
    old_state_sentinel->next_idx = note_info->next_idx;
    chart_get_note(chart, note_info->next_idx, old_state)->prev_idx = old_state_sentinel_index;
    // insert into new linked list
    note_info->next_idx = new_state_sentinel_index;
    note_info->prev_idx = new_state_sentinel->prev_idx;
    new_state_sentinel->prev_idx = index;
    chart_get_note(chart, note_info->prev_idx, new_state)->next_idx = index;
    // update state
    note_info->state = new_state;

    critical_exit(&chart->critical_section);
}

// if any note is blank, return a pointer to its memory, otherwise NULL.
void *chart_request_start(chart_t *chart) {
    return chart_next(chart, CHART_NOTE_BLANK);
}

// write back a request.
void chart_request_send(chart_t *chart, void *note) {
    chart_update(chart, note, CHART_NOTE_BLANK, CHART_NOTE_REQUEST);

    chart->notify_server();
}

// if any unhandled requests are available, return a pointer to the memory of one of them, otherwise NULL.
void *chart_reply_start(chart_t *chart) {
    return chart_next(chart, CHART_NOTE_REQUEST);
}

// write back a reply.
void chart_reply_send(chart_t *chart, void *note) {
    chart_update(chart, note, CHART_NOTE_REQUEST, CHART_NOTE_REPLY);

    chart->notify_client();
}

// acknowledgements are sent by the CLIENT

// if any unacknowledged requests are available, return a pointer to its memory, otherwise NULL.
void *chart_ack_start(chart_t *chart) {
    return chart_next(chart, CHART_NOTE_REPLY);
}

// write back an acknowledgement.
void chart_ack_send(chart_t *chart, void *note) {
    chart_update(chart, note, CHART_NOTE_BLANK, CHART_NOTE_REQUEST);

    chart->notify_server();
}
