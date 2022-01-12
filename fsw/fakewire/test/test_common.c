#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <hal/thread.h>
#include <fsw/init.h>

#include "test_common.h"

// so that we don't need a full clock implementation for the clock functions
int64_t clock_offset_adj = 0;

void test_common_make_fifos(const char *prefix) {
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

void spacecraft_init(void) {
    // do nothing
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <scratchdir>\n", argv[0]);
        return 1;
    }

    // change directories to simplify referencing
    if (chdir(argv[1]) < 0) {
        perror(argv[1]);
        return 1;
    }

    initialize_systems();
    start_predef_threads();

    if (test_main() == 0) {
        printf("Test passed!\n");
        return 0;
    } else {
        printf("TEST FAILED\n");
        return 1;
    }
}
