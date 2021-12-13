#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <hal/thread.h>
#include <fsw/debug.h>
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
    mutex_t out_mutex;
    struct packet_chain *chain_out;

    chart_t     read_chart;
    semaphore_t wake;
};

static void *exchange_reader(void *opaque) {
    struct reader_config *rc = (struct reader_config *) opaque;

    int last_packet_marker = 1;
    do {
        struct io_rx_ent *ent = chart_reply_start(&rc->read_chart);
        if (ent == NULL) {
            semaphore_take(&rc->wake);
            continue;
        }

        assert(ent->actual_length > 0 && ent->actual_length <= io_rx_size(&rc->read_chart));
        debugf(DEBUG, "[%s] Completed read of packet with length %ld", rc->name, ent->actual_length - 1);

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

        chart_reply_send(&rc->read_chart, 1);
    } while (last_packet_marker != 0);

    return NULL;
}

struct writer_config {
    const char *name;

    semaphore_t wake;
    chart_t     write_chart;

    struct packet_chain *chain_in;
    bool pass;
};

static void *exchange_writer(void *opaque) {
    struct writer_config *wc = (struct writer_config *) opaque;

    assert(wc->pass == false);

    struct packet_chain *chain = wc->chain_in;

    while (chain) {
        assert(chain->packet_len <= io_rx_size(&wc->write_chart) - 1);
        struct io_rx_ent *entry = chart_request_start(&wc->write_chart);
        assert(entry != NULL);

        entry->data[0] = (chain->next != NULL); // whether or not this is the last packet
        memcpy(&entry->data[1], chain->packet_data, chain->packet_len);

        debugf(DEBUG, "[%s] - Starting write of packet with length %lu", wc->name, chain->packet_len);
        entry->actual_length = chain->packet_len + 1;
        chart_request_send(&wc->write_chart, 1);
        while (chart_request_avail(&wc->write_chart) < chart_note_count(&wc->write_chart)) {
            semaphore_take(&wc->wake);
        }
        debugf(DEBUG, "[%s] Completed write of packet with length %lu", wc->name, chain->packet_len);

        chain = chain->next;
    }

    wc->pass = true;
    return NULL;
}

struct exchange_config {
    const char *name;
    char *path_buf;
    int flags;
    struct packet_chain *chain_in;
    struct packet_chain *chain_out;
    bool pass;
};

struct exchange_state {
    struct reader_config rc;
    struct writer_config wc;
    fw_exchange_t exc;
};

static void exchange_state_notify_reader(void *opaque) {
    struct exchange_state *est = (struct exchange_state *) opaque;
    assert(est != NULL);

    (void) semaphore_give(&est->rc.wake);
}

static void exchange_state_notify_writer(void *opaque) {
    struct exchange_state *est = (struct exchange_state *) opaque;
    assert(est != NULL);

    (void) semaphore_give(&est->wc.wake);
}

static void *exchange_controller(void *opaque) {
    struct exchange_config *ec = (struct exchange_config *) opaque;

    struct exchange_state *est = malloc(sizeof(struct exchange_state));
    assert(est != NULL);

    est->rc.name = ec->name;
    mutex_init(&est->rc.out_mutex);
    est->rc.chain_out = NULL;
    semaphore_init(&est->rc.wake);

    chart_init(&est->rc.read_chart, io_rx_pad_size(4096), 4);
    chart_attach_server(&est->rc.read_chart, exchange_state_notify_reader, est);

    est->wc.name = ec->name;
    semaphore_init(&est->wc.wake);
    chart_init(&est->wc.write_chart, io_rx_pad_size(4096), 4);
    chart_attach_client(&est->wc.write_chart, exchange_state_notify_writer, est);
    est->wc.chain_in = ec->chain_in;
    est->wc.pass = false;

    fw_link_options_t options = {
        .label = ec->name,
        .path  = ec->path_buf,
        .flags = ec->flags,
    };

    debugf(INFO, "[%s] initializing exchange...", ec->name);
    if (fakewire_exc_init(&est->exc, options, &est->rc.read_chart, &est->wc.write_chart) < 0) {
        debugf(CRITICAL, "[%s] could not initialize exchange", ec->name);
        ec->pass = false;
        ec->chain_out = NULL;
        return NULL;
    }
    debugf(DEBUG, "Attached!");

    pthread_t reader_thread;
    pthread_t writer_thread;
    thread_create(&reader_thread, "exc_reader", 1, exchange_reader, &est->rc, NOT_RESTARTABLE);
    thread_create(&writer_thread, "exc_writer", 1, exchange_writer, &est->wc, NOT_RESTARTABLE);

    struct timespec ts;
    thread_time_now(&ts);
    // wait up to five seconds
    ts.tv_sec += 5;

    bool pass = true;

    if (!thread_join_timed(reader_thread, &ts)) {
        debugf(CRITICAL, "[%s] exchange controller: could not join reader thread by 5 second deadline", ec->name);
        pass = false;
    }
    if (!thread_join_timed(writer_thread, &ts)) {
        debugf(CRITICAL, "[%s] exchange controller: could not join writer thread by 5 second deadline", ec->name);
        pass = false;
    } else if (!est->wc.pass) {
        debugf(CRITICAL, "[%s] exchange controller: failed due to writer failure", ec->name);
        pass = false;
    }

    ec->pass = pass;
    mutex_lock(&est->rc.out_mutex);
    ec->chain_out = reverse_chain(est->rc.chain_out);
    mutex_unlock(&est->rc.out_mutex);

    return NULL;
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

int test_main(void) {
    test_common_make_fifos("fwfifo");

    char path_buf[256];
    test_common_get_fifo("fwfifo", path_buf, sizeof(path_buf));

    srand(31415);

    struct exchange_config ec_left = {
        .name     = " left",
        .path_buf = path_buf,
        .flags    = FW_FLAG_FIFO_PROD,
        .chain_in = random_packet_chain(),
        // chain_out and pass to be filled in
    };
    struct exchange_config ec_right = {
        .name     = "right",
        .path_buf = path_buf,
        .flags    = FW_FLAG_FIFO_CONS,
        .chain_in = random_packet_chain(),
        // chain_out and pass to be filled in
    };

    pthread_t left, right;

    thread_create(&left, "ec_left", 1, exchange_controller, &ec_left, NOT_RESTARTABLE);
    thread_create(&right, "ec_right", 1, exchange_controller, &ec_right, NOT_RESTARTABLE);

    debugf(INFO, "Waiting for test to complete...");
    thread_join(left);
    thread_join(right);
    debugf(INFO, "Controller threads finished!");

    int code = 0;
    if (!ec_left.pass) {
        debugf(CRITICAL, "Left controller failed");
        code = -1;
    }
    if (!ec_right.pass) {
        debugf(CRITICAL, "Right controller failed");
        code = -1;
    }
    if (!compare_packet_chains("[left->right]", ec_left.chain_in, ec_right.chain_out)) {
        debugf(CRITICAL, "Invalid packet chain transmitted from left to right");
        code = -1;
    } else {
        debugf(INFO, "Valid packet chain transmitted from left to right.");
    }
    if (!compare_packet_chains("[right->left]", ec_right.chain_in, ec_left.chain_out)) {
        debugf(CRITICAL, "Invalid packet chain transmitted from right to left");
        code = -1;
    } else {
        debugf(INFO, "Valid packet chain transmitted from right to left.");
    }

    return code;
}
