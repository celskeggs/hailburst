#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "fakewire_exc.h"
#include "test_common.h"
#include "thread.h"

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
    fw_exchange_t *exc;
    struct packet_chain *chain_out;
};

static void *exchange_reader(void *opaque) {
    struct reader_config *rc = (struct reader_config *) opaque;
    uint8_t receive_buffer[4096];

    struct packet_chain *chain_out = NULL;

    while (true) {
        printf("[%s] - Started read of packet\n", rc->name);
        ssize_t actual_len = fakewire_exc_read(rc->exc, receive_buffer, sizeof(receive_buffer));
        if (actual_len < 0) {
            printf("[%s] Packet could not be read (actual_len=%ld); reader finished.\n", rc->name, actual_len);
            rc->chain_out = reverse_chain(chain_out);
            return NULL;
        }
        printf("[%s] Completed read of packet with length %ld\n", rc->name, actual_len - 1);
        assert(actual_len >= 1 && actual_len <= sizeof(receive_buffer));

        int last_packet_marker = receive_buffer[0];
        assert(last_packet_marker == 0 || last_packet_marker == 1);

        struct packet_chain *new_link = check_malloc(sizeof(struct packet_chain));
        if (actual_len > 0) {
            new_link->packet_data = check_malloc(actual_len - 1);
        } else {
            new_link->packet_data = NULL;
        }
        memcpy(new_link->packet_data, receive_buffer + 1, actual_len - 1);
        new_link->packet_len = actual_len - 1;
        // add to linked list
        new_link->next = chain_out;
        chain_out = new_link;

        if (last_packet_marker == 0) {
            rc->chain_out = reverse_chain(chain_out);
            return NULL;
        }
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

    struct packet_chain *chain = wc->chain_in;
    uint8_t send_buffer[4096];

    while (chain) {
        assert(chain->packet_len <= sizeof(send_buffer) - 1);
        send_buffer[0] = (chain->next != NULL); // whether or not the last packet
        memcpy(send_buffer+1, chain->packet_data, chain->packet_len);

        printf("[%s] - Started write of packet with length %lu\n", wc->name, chain->packet_len);
        if (fakewire_exc_write(wc->exc, send_buffer, chain->packet_len + 1) < 0) {
            fprintf(stderr, "failed during fakewire_exc_write\n");
            wc->pass = false;
            return NULL;
        }
        printf("[%s] Completed write of packet with length %lu\n", wc->name, chain->packet_len);

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

    fw_exchange_t exc;
    fakewire_exc_init(&exc, ec->name);
    printf("[%s] attaching...\n", ec->name);
    if (fakewire_exc_attach(&exc, ec->path_buf, ec->flags) < 0) {
        fakewire_exc_destroy(&exc);

        fprintf(stderr, "[%s] could not attach\n", ec->name);
        ec->pass = false;
        ec->chain_out = NULL;
        return NULL;
    }
    printf("Attached!\n");

    struct reader_config rc = {
        .name = ec->name,
        .exc  = &exc,
    };
    struct writer_config wc = {
        .name     = ec->name,
        .exc      = &exc,
        .chain_in = ec->chain_in,
    };

    pthread_t reader_thread;
    pthread_t writer_thread;
    thread_create(&reader_thread, exchange_reader, &rc);
    thread_create(&writer_thread, exchange_writer, &wc);

    struct timespec ts;
    thread_time_now(&ts);
    // wait up to five seconds
    ts.tv_sec += 5;

    bool pass = true;

    if (!thread_join_timed(reader_thread, &ts)) {
        fprintf(stderr, "[%s] exchange controller: could not join reader thread by 5 second deadline\n", ec->name);
        pass = false;
        // detach to force stop
        fakewire_exc_detach(&exc);
        fprintf(stderr, "[%s] exchange controller: performed force stop\n", ec->name);
        thread_join(reader_thread);
        fprintf(stderr, "[%s] exchange controller: joined with reader\n", ec->name);
        thread_join(writer_thread);
        fprintf(stderr, "[%s] exchange controller: joined with writer\n", ec->name);
    } else if (!thread_join_timed(writer_thread, &ts)) {
        fprintf(stderr, "[%s] exchange controller: could not join writer thread by 5 second deadline\n", ec->name);
        pass = false;
        // detach to force stop
        fakewire_exc_detach(&exc);
        fprintf(stderr, "[%s] exchange controller: performed force stop\n", ec->name);
        thread_join(writer_thread);
        fprintf(stderr, "[%s] exchange controller: joined with writer\n", ec->name);
    } else {
        // passed! detach to clean up.
        fprintf(stderr, "[%s] exchange controller: detaching to clean up\n", ec->name);
        fakewire_exc_detach(&exc);

        if (!wc.pass) {
            fprintf(stderr, "[%s] exchange controller: failed due to writer failure\n", ec->name);
            pass = false;
        }
    }

    // clean up
    fakewire_exc_destroy(&exc);

    ec->pass = pass;
    ec->chain_out = rc.chain_out;

    return NULL;
}

static struct packet_chain *random_packet_chain(void) {
    int packet_count = rand() % 20 + 10;

    struct packet_chain *out = NULL;
    printf("Generating packets...\n");
    for (int i = 0; i < packet_count; i++) {
        struct packet_chain *next = check_malloc(sizeof(struct packet_chain));
        size_t new_len = (rand() % 2 == 0) ? (rand() % 4000) : (rand() % 10);
        next->packet_len = new_len;
        next->packet_data = check_malloc(new_len);
        for (size_t j = 0; j < new_len; j++) {
            next->packet_data[j] = rand() % 256;
        }
        next->next = out;
        printf("[%d] => packet of size %lu\n", i, new_len);
        out = next;
    }
    printf("Generated packet chain of length %d\n", packet_count);

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
        fprintf(stderr, "Mismatch: out of %lu bytes, found %lu mismatches\n", i, mismatches);
    }
    if (baseline_len != actual_len) {
        fprintf(stderr, "Mismatch: packet length should have been %lu, but found %lu\n", baseline_len, actual_len);
        return false;
    }
    return mismatches == 0;
}

static bool compare_packet_chains(const char *prefix, struct packet_chain *baseline, struct packet_chain *actual) {
    bool ok = true;
    int nb = 0, na = 0;
    while (baseline != NULL && actual != NULL) {
        if (!compare_packets(baseline->packet_data, baseline->packet_len, actual->packet_data, actual->packet_len)) {
            fprintf(stderr, "%s mismatch: data error in packet %d received.\n", prefix, nb);
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
        fprintf(stderr, "%s mismatch: fewer packets received (%d) than sent (%d).\n", prefix, na, nb);
        ok = false;
    } else if (actual != NULL) {
        while (actual != NULL) {
            actual = actual->next;
            na++;
        }
        fprintf(stderr, "%s mismatch: more packets received (%d) than sent (%d).\n", prefix, na, nb);
        ok = false;
    }
    return ok;
}

static void free_packet_chain(struct packet_chain *chain) {
    while (chain != NULL) {
        struct packet_chain *next = chain->next;
        free(chain->packet_data);
        free(chain);
        chain = next;
    }
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

    thread_create(&left, exchange_controller, &ec_left);
    thread_create(&right, exchange_controller, &ec_right);

    printf("Waiting for test to complete...\n");
    thread_join(left);
    thread_join(right);
    printf("Controller threads finished!\n");

    int code = 0;
    if (!ec_left.pass) {
        fprintf(stderr, "Left controller failed\n");
        code = -1;
    }
    if (!ec_right.pass) {
        fprintf(stderr, "Right controller failed\n");
        code = -1;
    }
    if (!compare_packet_chains("[left->right]", ec_left.chain_in, ec_right.chain_out)) {
        fprintf(stderr, "Invalid packet chain transmitted from left to right\n");
        code = -1;
    } else {
        printf("Valid packet chain transmitted from left to right.\n");
    }
    if (!compare_packet_chains("[right->left]", ec_right.chain_in, ec_left.chain_out)) {
        fprintf(stderr, "Invalid packet chain transmitted from right to left\n");
        code = -1;
    } else {
        printf("Valid packet chain transmitted from right to left.\n");
    }
    free_packet_chain(ec_left.chain_in);
    free_packet_chain(ec_left.chain_out);
    free_packet_chain(ec_right.chain_in);
    free_packet_chain(ec_right.chain_out);

    return code;
}
