#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <hal/debug.h>

#include "fifo.h"

void test_fifo_make(const char *prefix) {
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
