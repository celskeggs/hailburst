#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "fakewire_link.h"
#include "test_common.h"
#include "thread.h"

static bool producer_pass = false;
static bool consumer_pass = false;
static bool abort_test = false;
static pthread_mutex_t pass_mutex;

static fw_char_t test_vectors[] = {
    FW_CTRL_ESC, FW_CTRL_FCT, // NULL
    't', 'e', 's', 't', '1', '2', '3',
    FW_CTRL_EOP,
    'x', 'y', 'z',
    FW_CTRL_EOP,
    FW_CTRL_ESC, FW_CTRL_FCT, // NULL
    0, 0xFF, 1, 0xFE, 2, 0xFD, 0x08, 0x80, 0xDE, 0xAD,
    FW_CTRL_EEP,
    FW_CTRL_ESC, FW_CTRL_FCT,

    // to skip
    FW_CTRL_ESC, FW_CTRL_FCT,
};
// cannot assume correct delivery of final bits sent
static int num_test_vectors_to_skip_verifying = 2;

static void *producer_thread(void *opaque) {
    char *path_buf = (char *) opaque;
    fw_link_t port;

    printf("Hello from producer thread! Attaching...\n");
    fakewire_link_attach(&port, path_buf, FW_FLAG_FIFO_PROD);
    printf("Producer attached!\n");

    for (int i = 0; i < sizeof(test_vectors) / sizeof(fw_char_t) && !abort_test; i++) {
        fakewire_link_write(&port, test_vectors[i]);
        // printf("Wrote character %d => %x\n", i, test_vectors[i]);
    }

    fakewire_link_detach(&port);

    mutex_lock(&pass_mutex);
    producer_pass = true;
    mutex_unlock(&pass_mutex);

    return NULL;
}

static void *consumer_thread(void *opaque) {
    char *path_buf = (char *) opaque;
    fw_link_t port;

    printf("Hello from consumer thread! Attaching...\n");
    fakewire_link_attach(&port, path_buf, FW_FLAG_FIFO_CONS);
    printf("Consumer attached!\n");

    int total = sizeof(test_vectors) / sizeof(fw_char_t);
    for (int i = 0; i < total && !abort_test; i++) {
        fw_char_t ch = fakewire_link_read(&port);
        if (ch == FW_PARITYFAIL) {
            if (i >= total - num_test_vectors_to_skip_verifying) {
                printf("Failed parity, but at an acceptable point.\n");
                break;
            }
            printf("Failed parity unexpectedly!\n");
            printf("Consumer FAIL\n");
            return NULL; // without signaling pass
        }
        if (ch != test_vectors[i]) {
            printf("Read character %d => %x (wanted %x)\n", i, ch, test_vectors[i]);
            printf("Consumer FAIL\n");
            return NULL; // without signaling pass
        }
        // printf("Read character %d => %x\n", i, ch);
    }

    fakewire_link_detach(&port);

    mutex_lock(&pass_mutex);
    consumer_pass = true;
    mutex_unlock(&pass_mutex);

    return NULL;
}

int test_main(void) {
    test_common_make_fifos("fwfifo");

    mutex_init(&pass_mutex);

    pthread_t producer, consumer;

    char path_buf[256];
    test_common_get_fifo("fwfifo", path_buf, sizeof(path_buf));
    thread_create(&producer, producer_thread, path_buf);
    thread_create(&consumer, consumer_thread, path_buf);

    printf("Waiting for test to complete...\n");
    for (int i = 0, stop_early = false; i < 5 && !stop_early; i++) {
        sleep(1);
        printf("Checking test results...\n");
        mutex_lock(&pass_mutex);
        stop_early = (producer_pass && consumer_pass);
        mutex_unlock(&pass_mutex);
    }

    int code = 0;
    mutex_lock(&pass_mutex);
    if (!producer_pass || !consumer_pass) {
        printf("TEST FAILED: producer=%s consumer=%s\n",
            producer_pass ? "pass" : "fail", consumer_pass ? "pass" : "fail");
        code = -1;
        abort_test = true;
        thread_cancel(producer);
        thread_cancel(consumer);
    }
    mutex_unlock(&pass_mutex);

    thread_join(producer);
    thread_join(consumer);

    return code;
}
