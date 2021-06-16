#ifndef APP_TLM_H
#define APP_TLM_H

#include <stdbool.h>
#include <stdint.h>
#include "comm.h"

typedef struct {
    uint64_t reading_time;
    int16_t  mag_x;
    int16_t  mag_y;
    int16_t  mag_z;
} tlm_mag_reading_t;

// initialize telemetry system
void telemetry_init(comm_enc_t *encoder);

// actual telemetry calls
void tlm_cmd_received(uint64_t original_timestamp, uint32_t original_command_id);
void tlm_cmd_completed(uint64_t original_timestamp, uint32_t original_command_id, bool success);
void tlm_cmd_not_recognized(uint64_t original_timestamp, uint32_t original_command_id, uint32_t length);
void tlm_pong(uint32_t ping_id);
void tlm_clock_calibrated(int64_t adjustment);
void tlm_mag_pwr_state_changed(bool power_state);
void tlm_mag_readings_array(tlm_mag_reading_t *readings, size_t num_readings);

#endif /* APP_TLM_H */
