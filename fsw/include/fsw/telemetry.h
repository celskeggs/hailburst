#ifndef FSW_TLM_H
#define FSW_TLM_H

#include <stdbool.h>
#include <stdint.h>

#include <hal/thread.h>
#include <fsw/comm.h>
#include <fsw/init.h>
#include <fsw/multichart.h>

typedef struct {
    uint64_t reading_time;
    int16_t  mag_x;
    int16_t  mag_y;
    int16_t  mag_z;
} tlm_mag_reading_t;

typedef struct {
    semaphore_t         sync_wake;
    multichart_client_t sync_client;
} tlm_sync_endpoint_t;

typedef struct {
    multichart_client_t client;
} tlm_async_endpoint_t;

// initialize telemetry system
void telemetry_init(comm_enc_t *encoder);

#define TELEMETRY_ASYNC_REGISTER(t_ident) \
    tlm_async_endpoint_t t_ident; \
    PROGRAM_INIT_PARAM(STAGE_CRAFT, tlm_async_init, t_ident, &t_ident)

#define TELEMETRY_SYNC_REGISTER(t_ident) \
    tlm_sync_endpoint_t t_ident; \
    PROGRAM_INIT_PARAM(STAGE_CRAFT, tlm_sync_init, t_ident, &t_ident)

// actual telemetry calls
void tlm_async_init(tlm_async_endpoint_t *tep);
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
void tlm_sync_init(tlm_sync_endpoint_t *tep);
void tlm_sync_mag_readings_map(tlm_sync_endpoint_t *tep, size_t *fetch_count,
                               void (*fetch)(void *param, size_t index, tlm_mag_reading_t *out), void *param);

#endif /* FSW_TLM_H */
