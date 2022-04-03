#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <hal/init.h>
#include <hal/thread.h>
#include <flight/clock.h>

// so that we don't need a full clock implementation for the clock functions
int64_t clock_offset_adj_fast = 0;

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
    enter_scheduler();

    // exit just the main thread, because returning causes all threads to exit, and we want everything to keep running
    pthread_exit(NULL);
}
