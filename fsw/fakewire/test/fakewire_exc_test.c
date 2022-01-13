#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <hal/thread.h>
#include <fsw/clock.h>
#include <fsw/debug.h>
#include <fsw/init.h>
#include <fsw/fakewire/exchange.h>

#include "test_common.h"

struct packet_chain {
    uint8_t *packet_data;
    size_t   packet_len;

    struct packet_chain *next;
};

static void *check_malloc(size_t len) {
    void *out = malloc(len);
    assert(out != NULL);
    return out;
}

static struct packet_chain *reverse_chain(struct packet_chain *chain) {
    struct packet_chain *reverse = NULL;

    while (chain != NULL) {
        // extract from existing chain
        struct packet_chain *c = chain;
        chain = c->next;
        // add to reversed chain
        c->next = reverse;
        reverse = c;
    }

    return reverse;
}

struct reader_config {
    const char *name;

    chart_t    *read_chart;
    semaphore_t wake;

    mutex_t out_mutex;
    struct packet_chain *chain_out;
    semaphore_t complete;
};

static void exchange_reader(void *opaque) {
    struct reader_config *rc = (struct reader_config *) opaque;

    int last_packet_marker = 1;
    do {
        struct io_rx_ent *ent = chart_reply_start(rc->read_chart);
        if (ent == NULL) {
            semaphore_take(&rc->wake);
            continue;
        }

        assert(ent->actual_length > 0 && ent->actual_length <= io_rx_size(rc->read_chart));
        debugf(DEBUG, "[%8s] Completed read of packet with length %ld", rc->name, ent->actual_length - 1);

        last_packet_marker = ent->data[0];
        assert(last_packet_marker == 0 || last_packet_marker == 1);

        struct packet_chain *new_link = check_malloc(sizeof(struct packet_chain));
        if (ent->actual_length > 0) {
            new_link->packet_data = check_malloc(ent->actual_length - 1);
        } else {
            new_link->packet_data = NULL;
        }
        memcpy(new_link->packet_data, ent->data + 1, ent->actual_length - 1);
        new_link->packet_len = ent->actual_length - 1;
        // add to linked list
        mutex_lock(&rc->out_mutex);
        new_link->next = rc->chain_out;
        rc->chain_out = new_link;
        mutex_unlock(&rc->out_mutex);

        chart_reply_send(rc->read_chart, 1);
    } while (last_packet_marker != 0);

    semaphore_give(&rc->complete);
}

struct writer_config {
    const char *name;

    semaphore_t wake;
    chart_t    *write_chart;

    struct packet_chain *chain_in;

    bool pass;
    semaphore_t complete;
};

static void exchange_writer(void *opaque) {
    struct writer_config *wc = (struct writer_config *) opaque;

    assert(wc->pass == false);

    struct packet_chain *chain = wc->chain_in;

    while (chain) {
        assert(chain->packet_len <= io_rx_size(wc->write_chart) - 1);
        struct io_rx_ent *entry = chart_request_start(wc->write_chart);
        assert(entry != NULL);

        entry->data[0] = (chain->next != NULL); // whether or not this is the last packet
        memcpy(&entry->data[1], chain->packet_data, chain->packet_len);

        debugf(DEBUG, "[%8s] - Starting write of packet with length %lu", wc->name, chain->packet_len);
        entry->actual_length = chain->packet_len + 1;
        chart_request_send(wc->write_chart, 1);
        while (chart_request_avail(wc->write_chart) < chart_note_count(wc->write_chart)) {
            semaphore_take(&wc->wake);
        }
        debugf(DEBUG, "[%8s] Completed write of packet with length %lu", wc->name, chain->packet_len);

        chain = chain->next;
    }

    wc->pass = true;
    semaphore_give(&wc->complete);
}

struct exchange_state {
    struct reader_config rc;
    struct writer_config wc;
};

static void exchange_state_notify_reader(struct exchange_state *est) {
    assert(est != NULL);

    (void) semaphore_give(&est->rc.wake);
}

static void exchange_state_notify_writer(struct exchange_state *est) {
    assert(est != NULL);

    (void) semaphore_give(&est->wc.wake);
}

static struct packet_chain *random_packet_chain(void) {
    int packet_count = rand() % 20 + 10;

    struct packet_chain *out = NULL;
    debugf(DEBUG, "Generating packets...");
    for (int i = 0; i < packet_count; i++) {
        struct packet_chain *next = check_malloc(sizeof(struct packet_chain));
        size_t new_len = (rand() % 2 == 0) ? (rand() % 4000) : (rand() % 10);
        next->packet_len = new_len;
        next->packet_data = check_malloc(new_len);
        for (size_t j = 0; j < new_len; j++) {
            next->packet_data[j] = rand() % 256;
        }
        next->next = out;
        debugf(DEBUG, "[%d] => packet of size %lu", i, new_len);
        out = next;
    }
    debugf(INFO, "Generated packet chain of length %d", packet_count);

    return out;
}

static unsigned int packet_chain_len(struct packet_chain *chain) {
    unsigned int count = 0;
    while (chain != NULL) {
        count++;
        chain = chain->next;
    }
    return count;
}

static bool compare_packets(uint8_t *baseline, size_t baseline_len, uint8_t *actual, size_t actual_len) {
    size_t i, mismatches = 0;
    for (i = 0; i < baseline_len && i < actual_len; i++) {
        if (baseline[i] != actual[i]) {
            mismatches++;
        }
    }
    if (mismatches > 0) {
        debugf(CRITICAL, "Mismatch: out of %lu bytes, found %lu mismatches", i, mismatches);
    }
    if (baseline_len != actual_len) {
        debugf(CRITICAL, "Mismatch: packet length should have been %lu, but found %lu", baseline_len, actual_len);
        return false;
    }
    return mismatches == 0;
}

static bool compare_packet_chains(const char *prefix, struct packet_chain *baseline, struct packet_chain *actual) {
    bool ok = true;
    int nb = 0, na = 0;
    while (baseline != NULL && actual != NULL) {
        if (!compare_packets(baseline->packet_data, baseline->packet_len, actual->packet_data, actual->packet_len)) {
            debugf(CRITICAL, "%s mismatch: data error in packet %d received.", prefix, nb);
            ok = false;
        }
        baseline = baseline->next;
        actual = actual->next;
        nb++;
        na++;
    }
    if (baseline != NULL) {
        while (baseline != NULL) {
            baseline = baseline->next;
            nb++;
        }
        debugf(CRITICAL, "%s mismatch: fewer packets received (%d) than sent (%d).", prefix, na, nb);
        ok = false;
    } else if (actual != NULL) {
        while (actual != NULL) {
            actual = actual->next;
            na++;
        }
        debugf(CRITICAL, "%s mismatch: more packets received (%d) than sent (%d).", prefix, na, nb);
        ok = false;
    }
    return ok;
}

static void prepare_test_fifos(void) {
    test_common_make_fifos("fwfifo");
}
PROGRAM_INIT(STAGE_RAW, prepare_test_fifos);

static void exchange_controller_init(struct exchange_state *es) {
    mutex_init(&es->rc.out_mutex);
    semaphore_init(&es->rc.wake);
    semaphore_init(&es->rc.complete);
    semaphore_init(&es->wc.wake);
    semaphore_init(&es->wc.complete);
    es->wc.chain_in = random_packet_chain();
}

#define EXCHANGE_CONTROLLER(e_ident, e_flags)                                                                   \
    CHART_REGISTER(e_ident ## _read, io_rx_pad_size(4096), 4);                                                  \
    CHART_REGISTER(e_ident ## _write, io_rx_pad_size(4096), 4);                                                 \
    struct exchange_state e_ident = {                                                                           \
        .rc = {                                                                                                 \
            .name = #e_ident,                                                                                   \
            .chain_out = NULL,                                                                                  \
            .read_chart = &e_ident ## _read,                                                                    \
        },                                                                                                      \
        .wc = {                                                                                                 \
            .name = #e_ident,                                                                                   \
            .write_chart = &e_ident ## _write,                                                                  \
            .pass = false,                                                                                      \
        },                                                                                                      \
    };                                                                                                          \
    CHART_SERVER_NOTIFY(e_ident ## _read, exchange_state_notify_reader, &e_ident);                              \
    CHART_CLIENT_NOTIFY(e_ident ## _write, exchange_state_notify_writer, &e_ident);                             \
    PROGRAM_INIT_PARAM(STAGE_READY, exchange_controller_init, e_ident, &e_ident);                               \
    const fw_link_options_t e_ident ## _options = {                                                             \
        .label = #e_ident,                                                                                      \
        .path = "./fwfifo",                                                                                     \
        .flags = e_flags,                                                                                       \
    };                                                                                                          \
    FAKEWIRE_EXCHANGE_REGISTER(e_ident ## _exchange, e_ident ## _options, e_ident ## _read, e_ident ## _write); \
    TASK_REGISTER(e_ident ## _reader_task, #e_ident "_reader", PRIORITY_INIT,                                   \
                  exchange_reader, &e_ident.rc, NOT_RESTARTABLE);                                              \
    TASK_REGISTER(e_ident ## _writer_task, #e_ident "_writer", PRIORITY_INIT,                                   \
                  exchange_writer, &e_ident.wc, NOT_RESTARTABLE)

// return true/false for pass/fail
static bool collect_status(struct exchange_state *est, struct packet_chain **chain_out, uint64_t deadline) {
    assert(est != NULL && chain_out != NULL);

    bool pass = true;

    // wait up to five seconds
    if (!semaphore_take_timed_abs(&est->rc.complete, deadline)) {
        debugf(CRITICAL, "[%8s] exchange controller: reader not complete by 5 second deadline", est->rc.name);
        pass = false;
    }
    if (!semaphore_take_timed_abs(&est->wc.complete, deadline)) {
        debugf(CRITICAL, "[%8s] exchange controller: writer not complete by 5 second deadline", est->wc.name);
        pass = false;
    } else if (!est->wc.pass) {
        debugf(CRITICAL, "[%8s] exchange controller: failed due to writer failure", est->wc.name);
        pass = false;
    }

    mutex_lock(&est->rc.out_mutex);
    *chain_out = reverse_chain(est->rc.chain_out);
    mutex_unlock(&est->rc.out_mutex);

    return pass;
}

static void init_random(void) {
    srand(31415);
}
PROGRAM_INIT(STAGE_RAW, init_random);

EXCHANGE_CONTROLLER(ec_left, FW_FLAG_FIFO_PROD);
EXCHANGE_CONTROLLER(ec_right, FW_FLAG_FIFO_CONS);

int test_main(void) {
    uint64_t deadline = clock_timestamp_monotonic() + 5000000000;

    int code = 0;
    struct packet_chain *left_out = NULL;
    struct packet_chain *right_out = NULL;
    debugf(INFO, "Waiting for test to complete...");
    if (!collect_status(&ec_left, &left_out, deadline)) {
        debugf(CRITICAL, "Left controller failed");
        code = -1;
    }
    if (!collect_status(&ec_right, &right_out, deadline)) {
        debugf(CRITICAL, "Right controller failed");
        code = -1;
    }
    debugf(INFO, "Controller threads finished!");

    if (!compare_packet_chains("[left->right]", ec_left.wc.chain_in, right_out)) {
        debugf(CRITICAL, "Invalid packet chain transmitted from left to right");
        code = -1;
    } else {
        debugf(INFO, "Valid packet chain of length %u transmitted from left to right.",
               packet_chain_len(ec_left.wc.chain_in));
    }
    if (!compare_packet_chains("[right->left]", ec_right.wc.chain_in, left_out)) {
        debugf(CRITICAL, "Invalid packet chain transmitted from right to left");
        code = -1;
    } else {
        debugf(INFO, "Valid packet chain of length %u transmitted from right to left.",
               packet_chain_len(ec_left.wc.chain_in));
    }

    return code;
}
