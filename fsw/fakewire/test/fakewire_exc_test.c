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
    semaphore_t finished;
};

static void exchange_recv(void *opaque, uint8_t *packet_data, size_t packet_length) {
    struct reader_config *rc = (struct reader_config *) opaque;

    debugf("[%s] Completed read of packet with length %ld", rc->name, packet_length - 1);
    assert(packet_length >= 1);

    int last_packet_marker = packet_data[0];
    assert(last_packet_marker == 0 || last_packet_marker == 1);

    struct packet_chain *new_link = check_malloc(sizeof(struct packet_chain));
    if (packet_length > 0) {
        new_link->packet_data = check_malloc(packet_length - 1);
    } else {
        new_link->packet_data = NULL;
    }
    memcpy(new_link->packet_data, packet_data + 1, packet_length - 1);
    new_link->packet_len = packet_length - 1;
    // add to linked list
    mutex_lock(&rc->out_mutex);
    new_link->next = rc->chain_out;
    rc->chain_out = new_link;
    mutex_unlock(&rc->out_mutex);

    if (last_packet_marker == 0) {
        semaphore_give(&rc->finished);
    }
}

struct writer_config {
    const char *name;
    fw_exchange_t *exc;
    struct packet_chain *chain_in;
    bool pass;
};

static void *exchange_writer(void *opaque) {
    struct writer_config *wc = (struct writer_config *) opaque;

    assert(wc->pass == false);

    struct packet_chain *chain = wc->chain_in;
    uint8_t send_buffer[4096];

    while (chain) {
        assert(chain->packet_len <= sizeof(send_buffer) - 1);
        send_buffer[0] = (chain->next != NULL); // whether or not the last packet
        memcpy(send_buffer+1, chain->packet_data, chain->packet_len);

        debugf("[%s] - Started write of packet with length %lu", wc->name, chain->packet_len);
        fakewire_exc_write(wc->exc, send_buffer, chain->packet_len + 1);
        debugf("[%s] Completed write of packet with length %lu", wc->name, chain->packet_len);

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

static void *exchange_controller(void *opaque) {
    struct exchange_config *ec = (struct exchange_config *) opaque;

    struct reader_config *rc = malloc(sizeof(struct reader_config));
    assert(rc != NULL);
    rc->name = ec->name;
    mutex_init(&rc->out_mutex);
    rc->chain_out = NULL;
    semaphore_init(&rc->finished);

    fw_exchange_options_t options = {
        .link_options = {
            .label = ec->name,
            .path  = ec->path_buf,
            .flags = ec->flags,
        },
        .recv_max_size = 4096,
        .recv_callback = exchange_recv,
        .recv_param    = rc,
    };
    fw_exchange_t *exc = malloc(sizeof(fw_exchange_t));
    assert(exc != NULL);
    debugf("[%s] initializing exchange...", ec->name);
    if (fakewire_exc_init(exc, options) < 0) {
        debugf("[%s] could not initialize exchange", ec->name);
        ec->pass = false;
        ec->chain_out = NULL;
        return NULL;
    }
    debug0("Attached!");

    struct writer_config *wc = malloc(sizeof(struct writer_config));
    assert(wc != NULL);
    wc->name = ec->name;
    wc->exc = exc;
    wc->chain_in = ec->chain_in;
    wc->pass = false;

    pthread_t writer_thread;
    thread_create(&writer_thread, "exc_writer", 1, exchange_writer, wc);

    struct timespec ts;
    thread_time_now(&ts);
    // wait up to five seconds
    ts.tv_sec += 5;

    bool pass = true;

    if (!semaphore_take_timed(&rc->finished, 5ull * NS_PER_SEC)) {
        debugf("[%s] exchange controller: did not receive completion notification from reader by 5 second deadline", ec->name);
        pass = false;
    }
    if (!thread_join_timed(writer_thread, &ts)) {
        debugf("[%s] exchange controller: could not join writer thread by 5 second deadline", ec->name);
        pass = false;
    } else if (!wc->pass) {
        debugf("[%s] exchange controller: failed due to writer failure", ec->name);
        pass = false;
    }

    ec->pass = pass;
    mutex_lock(&rc->out_mutex);
    ec->chain_out = reverse_chain(rc->chain_out);
    mutex_unlock(&rc->out_mutex);

    return NULL;
}

static struct packet_chain *random_packet_chain(void) {
    int packet_count = rand() % 20 + 10;

    struct packet_chain *out = NULL;
    debug0("Generating packets...");
    for (int i = 0; i < packet_count; i++) {
        struct packet_chain *next = check_malloc(sizeof(struct packet_chain));
        size_t new_len = (rand() % 2 == 0) ? (rand() % 4000) : (rand() % 10);
        next->packet_len = new_len;
        next->packet_data = check_malloc(new_len);
        for (size_t j = 0; j < new_len; j++) {
            next->packet_data[j] = rand() % 256;
        }
        next->next = out;
        debugf("[%d] => packet of size %lu", i, new_len);
        out = next;
    }
    debugf("Generated packet chain of length %d", packet_count);

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
        debugf("Mismatch: out of %lu bytes, found %lu mismatches", i, mismatches);
    }
    if (baseline_len != actual_len) {
        debugf("Mismatch: packet length should have been %lu, but found %lu", baseline_len, actual_len);
        return false;
    }
    return mismatches == 0;
}

static bool compare_packet_chains(const char *prefix, struct packet_chain *baseline, struct packet_chain *actual) {
    bool ok = true;
    int nb = 0, na = 0;
    while (baseline != NULL && actual != NULL) {
        if (!compare_packets(baseline->packet_data, baseline->packet_len, actual->packet_data, actual->packet_len)) {
            debugf("%s mismatch: data error in packet %d received.", prefix, nb);
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
        debugf("%s mismatch: fewer packets received (%d) than sent (%d).", prefix, na, nb);
        ok = false;
    } else if (actual != NULL) {
        while (actual != NULL) {
            actual = actual->next;
            na++;
        }
        debugf("%s mismatch: more packets received (%d) than sent (%d).", prefix, na, nb);
        ok = false;
    }
    return ok;
}

int test_main(void) {
    test_common_make_fifos("fwfifo");

    char path_buf[256];
    test_common_get_fifo("fwfifo", path_buf, sizeof(path_buf));

    wakeup_system_init();

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

    thread_create(&left, "ec_left", 1, exchange_controller, &ec_left);
    thread_create(&right, "ec_right", 1, exchange_controller, &ec_right);

    debug0("Waiting for test to complete...");
    thread_join(left);
    thread_join(right);
    debug0("Controller threads finished!");

    int code = 0;
    if (!ec_left.pass) {
        debug0("Left controller failed");
        code = -1;
    }
    if (!ec_right.pass) {
        debug0("Right controller failed");
        code = -1;
    }
    if (!compare_packet_chains("[left->right]", ec_left.chain_in, ec_right.chain_out)) {
        debug0("Invalid packet chain transmitted from left to right");
        code = -1;
    } else {
        debug0("Valid packet chain transmitted from left to right.");
    }
    if (!compare_packet_chains("[right->left]", ec_right.chain_in, ec_left.chain_out)) {
        debug0("Invalid packet chain transmitted from right to left");
        code = -1;
    } else {
        debug0("Valid packet chain transmitted from right to left.");
    }

    return code;
}
