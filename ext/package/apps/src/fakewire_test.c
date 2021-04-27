
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "fakewire.h"

static void make_test_fifos(const char *basepath) {
    char path_buf[strlen(basepath) + 10];

    snprintf(path_buf, sizeof(path_buf), "%s-p2c.pipe", basepath);
    if (mkfifo(path_buf, 0755) < 0) {
        perror("mkfifo");
        exit(1);
    }

    snprintf(path_buf, sizeof(path_buf), "%s-c2p.pipe", basepath);
    if (mkfifo(path_buf, 0755) < 0) {
        perror("mkfifo");
        exit(1);
    }
}

static bool producer_pass = false;
static bool consumer_pass = false;
static pthread_mutex_t pass_mutex = PTHREAD_MUTEX_INITIALIZER;

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
    fw_port_t port;

    printf("Hello from producer thread! Attaching...\n");
    fakewire_attach(&port, path_buf, FW_FLAG_FIFO_PROD);
    printf("Producer attached!\n");

    for (int i = 0; i < sizeof(test_vectors) / sizeof(fw_char_t); i++) {
        fakewire_write(&port, test_vectors[i]);
        // printf("Wrote character %d => %x\n", i, test_vectors[i]);
    }

    fakewire_detach(&port);

    if (pthread_mutex_lock(&pass_mutex) < 0) {
        perror("pthread_mutex_lock");
        exit(1);
    }
    producer_pass = true;
    if (pthread_mutex_unlock(&pass_mutex) < 0) {
        perror("pthread_mutex_unlock");
        exit(1);
    }

    return NULL;
}

static void *consumer_thread(void *opaque) {
    char *path_buf = (char *) opaque;
    fw_port_t port;

    printf("Hello from consumer thread! Attaching...\n");
    fakewire_attach(&port, path_buf, FW_FLAG_FIFO_CONS);
    printf("Consumer attached!\n");

    int total = sizeof(test_vectors) / sizeof(fw_char_t);
    for (int i = 0; i < total; i++) {
        fw_char_t ch = fakewire_read(&port);
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

    fakewire_detach(&port);

    if (pthread_mutex_lock(&pass_mutex) < 0) {
        perror("pthread_mutex_lock");
        exit(1);
    }
    consumer_pass = true;
    if (pthread_mutex_unlock(&pass_mutex) < 0) {
        perror("pthread_mutex_unlock");
        exit(1);
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <scratchdir>\n", argv[0]);
        return 1;
    }

    const char *scratchdir = argv[1];

    struct stat sb;
    if (stat(scratchdir, &sb) < 0) {
        perror(scratchdir);
        return 1;
    } else if ((sb.st_mode & S_IFMT) != S_IFDIR) {
        fprintf(stderr, "expected '%s' to be a directory\n", scratchdir);
        return 1;
    }

    char path_buf[strlen(scratchdir) + 10];
    snprintf(path_buf, sizeof(path_buf), "%s/fwfifo", scratchdir);
    make_test_fifos(path_buf);

    pthread_t producer, consumer;
    if (pthread_create(&producer, NULL, producer_thread, path_buf) < 0) {
        perror("pthread_create");
        return 1;
    }
    if (pthread_create(&consumer, NULL, consumer_thread, path_buf) < 0) {
        perror("pthread_create");
        return 1;
    }

    printf("Waiting for test to complete...\n");
    for (int i = 0; i < 5; i++) {
        sleep(1);
        printf("Checking test results...\n");
        if (pthread_mutex_lock(&pass_mutex) < 0) {
            perror("pthread_mutex_lock");
            return 1;
        }
        bool ok = (producer_pass && consumer_pass);
        if (pthread_mutex_unlock(&pass_mutex) < 0) {
            perror("pthread_mutex_unlock");
            return 1;
        }
        if (ok) {
            break;
        }
    }

    if (pthread_mutex_lock(&pass_mutex) < 0) {
        perror("pthread_mutex_lock");
        return 1;
    }
    if (!producer_pass || !consumer_pass) {
        printf("TEST FAILED: producer=%s consumer=%s\n",
            producer_pass ? "pass" : "fail", consumer_pass ? "pass" : "fail");
        return 1;
    }
    if (pthread_mutex_unlock(&pass_mutex) < 0) {
        perror("pthread_mutex_unlock");
        return 1;
    }

    if (pthread_join(producer, NULL) < 0 || pthread_join(consumer, NULL) < 0) {
        perror("pthread_join");
        return 1;
    }
    printf("Test passed!\n");

    return 0;
}
