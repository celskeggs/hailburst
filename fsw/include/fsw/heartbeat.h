#ifndef FSW_HEARTBEAT_H
#define FSW_HEARTBEAT_H

#include <stdint.h>

#include <hal/thread.h>
#include <fsw/telemetry.h>

typedef struct {
    tlm_async_endpoint_t telemetry;
    thread_t             thread;
} heartbeat_t;

void heartbeat_init(heartbeat_t *heart);

#endif /* FSW_HEARTBEAT_H */
