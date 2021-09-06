#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "test_common.h"

// so that we don't need a full clock implementation for the clock functions
int64_t clock_offset_adj = 0;

static const char *test_common_scratch_dir = NULL;

void test_common_make_fifos(const char *infix) {
    char path_buf[strlen(test_common_scratch_dir) + strlen(infix) + 20];

    test_common_get_fifo_p2c(infix, path_buf, sizeof(path_buf));
    if (mkfifo(path_buf, 0755) < 0) {
        perror("mkfifo");
        exit(1);
    }

    test_common_get_fifo_c2p(infix, path_buf, sizeof(path_buf));
    if (mkfifo(path_buf, 0755) < 0) {
        perror("mkfifo");
        exit(1);
    }
}

void test_common_get_fifo(const char *infix, char *out, size_t len) {
    assert(test_common_scratch_dir != NULL);

    int actual = snprintf(out, len, "%s/%s", test_common_scratch_dir, infix);
    assert(actual < len); // no overflow
}

void test_common_get_fifo_p2c(const char *infix, char *out, size_t len) {
    assert(test_common_scratch_dir != NULL);

    int actual = snprintf(out, len, "%s/%s-p2c.pipe", test_common_scratch_dir, infix);
    assert(actual < len); // no overflow
}

void test_common_get_fifo_c2p(const char *infix, char *out, size_t len) {
    assert(test_common_scratch_dir != NULL);

    int actual = snprintf(out, len, "%s/%s-c2p.pipe", test_common_scratch_dir, infix);
    assert(actual < len); // no overflow
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <scratchdir>\n", argv[0]);
        return 1;
    }

    test_common_scratch_dir = argv[1];

    struct stat sb;
    if (stat(test_common_scratch_dir, &sb) < 0) {
        perror(test_common_scratch_dir);
        return 1;
    } else if ((sb.st_mode & S_IFMT) != S_IFDIR) {
        fprintf(stderr, "expected '%s' to be a directory\n", test_common_scratch_dir);
        return 1;
    }

    if (test_main() == 0) {
        printf("Test passed!\n");
        return 0;
    } else {
        printf("TEST FAILED\n");
        return 1;
    }
}
