#ifndef APP_HEARTBEAT_H
#define APP_HEARTBEAT_H

#include <stdint.h>

#include "thread.h"

typedef struct {
    thread_t thread;
} heartbeat_t;

void heartbeat_init(heartbeat_t *heart);

#endif /* APP_HEARTBEAT_H */
