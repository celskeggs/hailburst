#include <stdio.h>

#include <hal/debug.h>
#include <hal/init.h>
#include <flight/spacecraft.h>

int main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    freopen("/dev/console", "w", stdout);
    freopen("/dev/console", "w", stderr);

    debugf(CRITICAL, "Initializing...");

    initialize_systems();
    start_predef_threads();

    // exit just the main thread, because returning causes all threads to exit, and we want everything to keep running
    pthread_exit(NULL);
}
