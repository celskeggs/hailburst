#include <unistd.h>

#include "debug.h"
#include "heartbeat.h"
#include "tlm.h"

static void *heartbeat_mainloop(void *opaque) {
    (void) opaque;

    debug0("Welcome to heartbeat_mainloop");

    // beat every 120 milliseconds (requirement is 150 milliseconds, so this is plenty fast)
    for (;;) {
        tlm_heartbeat();

        usleep(120 * 1000);
    }

    return NULL;
}

void heartbeat_init(heartbeat_t *heart) {
    debug0("Calling heartbeat_init");
    thread_create(&heart->thread, "heartbeat_loop", heartbeat_mainloop, NULL);
}
