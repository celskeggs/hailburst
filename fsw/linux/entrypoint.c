#include <stdio.h>

#include <fsw/debug.h>
#include <fsw/spacecraft.h>

int main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

	freopen("/dev/console", "w", stdout);
	freopen("/dev/console", "w", stderr);

    debugf(CRITICAL, "Initializing...");

    spacecraft_init();

    // exit just the main thread, because returning causes all threads to exit
    pthread_exit(NULL);
}
