#ifndef FSW_HEARTBEAT_H
#define FSW_HEARTBEAT_H

#include <stdint.h>

#include <hal/thread.h>

typedef struct {
    thread_t thread;
} heartbeat_t;

void heartbeat_init(heartbeat_t *heart);

#endif /* FSW_HEARTBEAT_H */
