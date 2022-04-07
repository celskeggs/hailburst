#include <stdio.h>

#include <hal/debug.h>
#include <hal/init.h>
#include <hal/thread.h>

int main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    freopen("/dev/console", "w", stdout);
    freopen("/dev/console", "w", stderr);

    debugf(CRITICAL, "Initializing...");

    initialize_systems();
    enter_scheduler();
}
