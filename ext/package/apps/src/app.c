#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "app.h"

static TaskSpec tasks_entry[] = {
    // { .name = "task_scrub_memory", .func = task_scrub_memory },
    { .name = "task_iotest_transmitter", .func = task_iotest_transmitter },
    { .name = "task_iotest_receiver", .func = task_iotest_receiver },
};

static void *task_wrapper(void *opaque) {
    TaskSpec *s = (TaskSpec *) opaque;

    fprintf(stderr, "Starting task: %s\n", s->name);
    s->func();
    fprintf(stderr, "Task exited: %s\n", s->name);

    return NULL;
}

int main(int argc, char *argv[]) {
	freopen("/dev/console", "w", stdout);
	freopen("/dev/console", "w", stderr);

    // TODO: come up with a standard interface for this
    init_iotest();

    pthread_t threads[sizeof(tasks_entry) / sizeof(*tasks_entry)];

    for (int i = 0; i < sizeof(tasks_entry) / sizeof(*tasks_entry); i++) {
        TaskSpec *s = &tasks_entry[i];
        if (pthread_create(&threads[i], NULL, task_wrapper, s) < 0) {
            perror("pthread_create");
            return 1;
        }
    }
    fprintf(stderr, "All tasks dispatched.\n");

    for (int i = 0; i < sizeof(tasks_entry) / sizeof(*tasks_entry); i++) {
        if (pthread_join(threads[i], NULL) < 0) {
            perror("pthread_join");
            return 1;
        }
        fprintf(stderr, "Task joined: %s\n", tasks_entry[i].name);
    }
    fprintf(stderr, "All tasks stopped.\n");

    return 0;
}
