#include <stdio.h>

#include <fsw/debug.h>
#include <fsw/init.h>
#include <fsw/spacecraft.h>

extern struct thread_st tasktable_start[];
extern struct thread_st tasktable_end[];

int main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

	freopen("/dev/console", "w", stdout);
	freopen("/dev/console", "w", stderr);

    debugf(CRITICAL, "Initializing...");

    initialize_systems();

    debugf(DEBUG, "Starting %u predefined threads...", tasktable_end - tasktable_start);
    for (thread_t task = tasktable_start; task < tasktable_end; task++) {
        thread_start_internal(task);
    }
    debugf(DEBUG, "Pre-registered threads started!");

    // exit just the main thread, because returning causes all threads to exit, and we want everything to keep running
    pthread_exit(NULL);
}
