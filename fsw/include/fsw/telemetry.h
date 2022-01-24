#ifndef FSW_TLM_H
#define FSW_TLM_H

#include <stdbool.h>
#include <stdint.h>

#include <hal/thread.h>
#include <fsw/comm.h>
#include <fsw/init.h>
#include <fsw/multichart.h>

enum {
    TLM_MAX_ASYNC_CLIENT_BUFFERS = 128,
    TLM_MAX_ASYNC_SIZE           = 16,
    TLM_MAX_SYNC_BUFFERS         = 1,
    TLM_MAX_SYNC_SIZE            = 64 * 1024,
    TLM_SYNC_NOTE_COUNT          = 1,
};

typedef struct {
    uint32_t telemetry_id;
    uint32_t data_len;
    uint8_t  data_bytes[TLM_MAX_ASYNC_SIZE];
} tlm_async_t;

typedef struct {
    uint32_t telemetry_id;
    uint32_t data_len;
    uint8_t  data_bytes[TLM_MAX_SYNC_SIZE];
} tlm_sync_t;

typedef struct {
    uint64_t reading_time;
    int16_t  mag_x;
    int16_t  mag_y;
    int16_t  mag_z;
} tlm_mag_reading_t;

typedef struct {
    thread_t             client_task;
    multichart_client_t *sync_client;
} tlm_sync_endpoint_t;

typedef struct {
    multichart_client_t *client;
} tlm_async_endpoint_t;

// initialize telemetry system
void telemetry_init(comm_enc_t *encoder);

#define TELEMETRY_ASYNC_REGISTER(t_ident)                                                      \
    /* no notification needs to be sent; asynchronous telemetry messages do not block */       \
    MULTICHART_CLIENT_REGISTER(t_ident ## _client, telemetry_async_chart, sizeof(tlm_async_t), \
                               TLM_MAX_ASYNC_CLIENT_BUFFERS, ignore_callback, NULL);           \
    tlm_async_endpoint_t t_ident = {                                                           \
        .client = &t_ident ## _client,                                                         \
    }

#define TELEMETRY_SYNC_REGISTER(t_ident, t_task)                                             \
    MULTICHART_CLIENT_REGISTER(t_ident ## _client, telemetry_sync_chart, sizeof(tlm_sync_t), \
                               TLM_SYNC_NOTE_COUNT, local_rouse, &t_task);                   \
    tlm_sync_endpoint_t t_ident = {                                                          \
        .client_task = &t_task,                                                              \
        .sync_client = &t_ident ## _client,                                                  \
    }

TASK_PROTO(telemetry_task);

#define TELEMETRY_SCHEDULE()                                                                 \
    TASK_SCHEDULE(telemetry_task)

// actual telemetry calls
void tlm_cmd_received(tlm_async_endpoint_t *tep, uint64_t original_timestamp, uint32_t original_command_id);
void tlm_cmd_completed(tlm_async_endpoint_t *tep, uint64_t original_timestamp, uint32_t original_command_id,
                       bool success);
void tlm_cmd_not_recognized(tlm_async_endpoint_t *tep, uint64_t original_timestamp, uint32_t original_command_id,
                            uint32_t length);
void tlm_pong(tlm_async_endpoint_t *tep, uint32_t ping_id);
void tlm_clock_calibrated(tlm_async_endpoint_t *tep, int64_t adjustment);
void tlm_heartbeat(tlm_async_endpoint_t *tep);
void tlm_mag_pwr_state_changed(tlm_async_endpoint_t *tep, bool power_state);

// synchronous telemetry writes
void tlm_sync_mag_readings_map(tlm_sync_endpoint_t *tep, size_t *fetch_count,
                               void (*fetch)(void *param, size_t index, tlm_mag_reading_t *out), void *param);

#endif /* FSW_TLM_H */
