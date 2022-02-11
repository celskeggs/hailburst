#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <hal/atomic.h>
#include <hal/clock.h>
#include <hal/debug.h>
#include <hal/init.h>
#include <hal/thread.h>
#include <bus/exchange.h>

static void make_fifos(const char *prefix) {
    char path_buf[strlen(prefix) + 20];

    size_t actual = snprintf(path_buf, sizeof(path_buf), "./%s-p2c.pipe", prefix);
    assert(actual < sizeof(path_buf)); // no overflow
    if (mkfifo(path_buf, 0755) < 0) {
        perror("mkfifo");
        exit(1);
    }

    actual = snprintf(path_buf, sizeof(path_buf), "./%s-c2p.pipe", prefix);
    assert(actual < sizeof(path_buf)); // no overflow
    if (mkfifo(path_buf, 0755) < 0) {
        perror("mkfifo");
        exit(1);
    }
}

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

    mutex_t out_mutex;
    struct packet_chain *chain_out;
    bool complete_flag;
    thread_t complete_notify;
};

static void exchange_reader(struct reader_config *rc) {
    int last_packet_marker = 1;
    do {
        struct io_rx_ent *ent = chart_reply_start(rc->read_chart);
        if (ent == NULL) {
            task_doze();
            continue;
        }

        assert(ent->actual_length > 0 && ent->actual_length <= io_rx_size(rc->read_chart));
        debugf(DEBUG, "[%8s] - Completed read of packet with length %d", rc->name, ent->actual_length - 1);

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

    atomic_store(rc->complete_flag, true);
    task_rouse(rc->complete_notify);
}

struct writer_config {
    const char *name;

    chart_t    *write_chart;

    struct packet_chain *chain_in;

    bool pass;
    bool complete_flag;
    thread_t complete_notify;
};

static void exchange_writer(struct writer_config *wc) {
    assert(wc->pass == false);

    struct packet_chain *chain = wc->chain_in;

    while (chain) {
        assert(chain->packet_len <= io_rx_size(wc->write_chart) - 1);
        struct io_rx_ent *entry;
        while ((entry = chart_request_start(wc->write_chart)) == NULL) {
            task_doze();
        }
        assert(entry != NULL);

        entry->data[0] = (chain->next != NULL); // whether or not this is the last packet
        memcpy(&entry->data[1], chain->packet_data, chain->packet_len);

        debugf(DEBUG, "[%8s] - Starting write of packet with length %lu", wc->name, chain->packet_len);
        entry->actual_length = chain->packet_len + 1;
        chart_request_send(wc->write_chart, 1);
        debugf(DEBUG, "[%8s] - Dispatched write of packet with length %lu", wc->name, chain->packet_len);

        chain = chain->next;
    }

    wc->pass = true;
    atomic_store(wc->complete_flag, true);
    task_rouse(wc->complete_notify);
}

struct exchange_config {
    struct reader_config rc;
    struct writer_config wc;
};

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
    make_fifos("fwfifo");
}
PROGRAM_INIT(STAGE_RAW, prepare_test_fifos);

static void exchange_controller_init(struct exchange_config *es) {
    mutex_init(&es->rc.out_mutex);
    es->wc.chain_in = random_packet_chain();
}

#define EXCHANGE_CONTROLLER(e_ident, e_flags, e_complete_task)                                                  \
    CHART_REGISTER(e_ident ## _read, io_rx_pad_size(4096), 4);                                                  \
    CHART_REGISTER(e_ident ## _write, io_rx_pad_size(4096), 4);                                                 \
    struct exchange_config e_ident = {                                                                          \
        .rc = {                                                                                                 \
            .name = #e_ident,                                                                                   \
            .chain_out = NULL,                                                                                  \
            .read_chart = &e_ident ## _read,                                                                    \
            .complete_flag = false,                                                                             \
            .complete_notify = &e_complete_task,                                                                \
        },                                                                                                      \
        .wc = {                                                                                                 \
            .name = #e_ident,                                                                                   \
            .write_chart = &e_ident ## _write,                                                                  \
            .pass = false,                                                                                      \
            .complete_flag = false,                                                                             \
            .complete_notify = &e_complete_task,                                                                \
        },                                                                                                      \
    };                                                                                                          \
    PROGRAM_INIT_PARAM(STAGE_READY, exchange_controller_init, e_ident, &e_ident);                               \
    const fw_link_options_t e_ident ## _options = {                                                             \
        .label = #e_ident,                                                                                      \
        .path = "./fwfifo",                                                                                     \
        .flags = e_flags,                                                                                       \
    };                                                                                                          \
    FAKEWIRE_EXCHANGE_REGISTER(e_ident ## _exchange, e_ident ## _options, e_ident ## _read, e_ident ## _write); \
    TASK_REGISTER(e_ident ## _reader_task, #e_ident "_reader", exchange_reader, &e_ident.rc, NOT_RESTARTABLE);  \
    TASK_REGISTER(e_ident ## _writer_task, #e_ident "_writer", exchange_writer, &e_ident.wc, NOT_RESTARTABLE);  \
    CHART_SERVER_NOTIFY(e_ident ## _read, task_rouse, &e_ident ## _reader_task);                                \
    CHART_CLIENT_NOTIFY(e_ident ## _write, task_rouse, &e_ident ## _writer_task)

// return true/false for pass/fail
static bool collect_status(struct exchange_config *est, struct packet_chain **chain_out, uint64_t deadline) {
    assert(est != NULL && chain_out != NULL);

    bool pass = true;

    // wait up to five seconds
    while (clock_timestamp_monotonic() < deadline) {
        if (atomic_load(est->rc.complete_flag) && atomic_load(est->wc.complete_flag)) {
            break;
        }
        (void) task_doze_timed_abs(deadline);
    }
    if (!atomic_load(est->rc.complete_flag)) {
        debugf(CRITICAL, "[%8s] exchange controller: reader not complete by 5 second deadline", est->rc.name);
        pass = false;
    }
    if (!atomic_load(est->wc.complete_flag)) {
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

TASK_PROTO(task_main);

EXCHANGE_CONTROLLER(ec_left, FW_FLAG_FIFO_PROD, task_main);
EXCHANGE_CONTROLLER(ec_right, FW_FLAG_FIFO_CONS, task_main);

static void test_main(void) {
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

    if (code != 0) {
        printf("TEST FAILED\n");
        exit(1);
    } else {
        printf("Test passed!\n");
        exit(0);
    }
}

TASK_REGISTER(task_main, "test_main", test_main, NULL, NOT_RESTARTABLE);
