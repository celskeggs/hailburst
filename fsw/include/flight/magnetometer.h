#ifndef FSW_MAGNETOMETER_H
#define FSW_MAGNETOMETER_H

#include <stdbool.h>

#include <hal/thread.h>
#include <bus/rmap.h>
#include <bus/switch.h>
#include <flight/telemetry.h>

enum {
    MAGNETOMETER_MAX_READINGS = 100,
};

typedef struct {
    rmap_t *endpoint;

    // synchronization
    bool should_be_powered;

    // telemetry buffer
    chart_t *readings;

    // telemetry output endpoint
    tlm_async_endpoint_t *telemetry_async;
    tlm_sync_endpoint_t  *telemetry_sync;
} magnetometer_t;

void magnetometer_drop_notification(void);
void magnetometer_query_loop(magnetometer_t *mag);
void magnetometer_telem_loop(magnetometer_t *mag);

#define MAGNETOMETER_REGISTER(m_ident, m_address, m_switch_in, m_switch_out, m_switch_port)                           \
    CHART_REGISTER(m_ident ## _readings, sizeof(tlm_mag_reading_t), MAGNETOMETER_MAX_READINGS);                       \
    /* we're only using the chart as a datastructure, so no need for notifications. */                                \
    CHART_SERVER_NOTIFY(m_ident ## _readings, ignore_callback, NULL);                                                 \
    CHART_CLIENT_NOTIFY(m_ident ## _readings, ignore_callback, NULL);                                                 \
    TELEMETRY_ASYNC_REGISTER(m_ident ## _telemetry_async);                                                            \
    extern magnetometer_t m_ident;                                                                                    \
    TASK_REGISTER(m_ident ## _telem, magnetometer_telem_loop, &m_ident, RESTARTABLE);                                 \
    TASK_REGISTER(m_ident ## _query, magnetometer_query_loop, &m_ident, RESTARTABLE);                                 \
    TELEMETRY_SYNC_REGISTER(m_ident ## _telemetry_sync, m_ident ## _telem);                                           \
    RMAP_ON_SWITCHES(m_ident ## _endpoint, "magnet", m_switch_in, m_switch_out, m_switch_port, m_address, 8, 4);      \
    magnetometer_t m_ident = {                                                                                        \
        .endpoint = &m_ident ## _endpoint,                                                                            \
        .should_be_powered = false,                                                                                   \
        .readings = &m_ident ## _readings,                                                                            \
        .telemetry_async = &m_ident ## _telemetry_async,                                                              \
        .telemetry_sync = &m_ident ## _telemetry_sync,                                                                \
    };                                                                                                                \

// one RMAP channel
#define MAGNETOMETER_MAX_IO_FLOW       RMAP_MAX_IO_FLOW

// largest packet size that the switch needs to be able to route
#define MAGNETOMETER_MAX_IO_PACKET                                                                                    \
    RMAP_MAX_IO_PACKET(8, 4)

#define MAGNETOMETER_SCHEDULE(m_ident)                                                                                \
    TASK_SCHEDULE(m_ident ## _query, 100)                                                                             \
    TASK_SCHEDULE(m_ident ## _telem, 100)

void magnetometer_set_powered(magnetometer_t *mag, bool powered);

#endif /* FSW_MAGNETOMETER_H */
