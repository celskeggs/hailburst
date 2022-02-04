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
    rmap_t     *endpoint;
    rmap_addr_t address;

    // synchronization
    bool should_be_powered;
    thread_t query_task;

    // telemetry buffer
    chart_t *readings;

    // telemetry output endpoint
    tlm_async_endpoint_t *telemetry_async;
    tlm_sync_endpoint_t  *telemetry_sync;
} magnetometer_t;

void magnetometer_drop_notification(void);
void magnetometer_query_loop(magnetometer_t *mag);
void magnetometer_telem_loop(magnetometer_t *mag);

#define MAGNETOMETER_REGISTER(m_ident, m_address, m_switch, m_switch_port)                              \
    CHART_REGISTER(m_ident ## _readings, sizeof(tlm_mag_reading_t), MAGNETOMETER_MAX_READINGS);         \
    /* we're only using the chart as a datastructure, so no need for notifications. */                  \
    CHART_SERVER_NOTIFY(m_ident ## _readings, ignore_callback, NULL);                                   \
    CHART_CLIENT_NOTIFY(m_ident ## _readings, ignore_callback, NULL);                                   \
    TELEMETRY_ASYNC_REGISTER(m_ident ## _telemetry_async);                                              \
    extern magnetometer_t m_ident;                                                                      \
    TASK_REGISTER(m_ident ## _telem, "mag_telem_loop", magnetometer_telem_loop, &m_ident, RESTARTABLE); \
    TASK_REGISTER(m_ident ## _query, "mag_query_loop", magnetometer_query_loop, &m_ident, RESTARTABLE); \
    TELEMETRY_SYNC_REGISTER(m_ident ## _telemetry_sync, m_ident ## _telem);                             \
    RMAP_ON_SWITCH(m_ident ## _endpoint, m_switch, m_switch_port, 8, 4, m_ident ## _query);             \
    magnetometer_t m_ident = {                                                                          \
        .endpoint = &m_ident ## _endpoint,                                                              \
        .address = (m_address),                                                                         \
        .should_be_powered = false,                                                                     \
        .query_task = &m_ident ## _query,                                                               \
        .readings = &m_ident ## _readings,                                                              \
        .telemetry_async = &m_ident ## _telemetry_async,                                                \
        .telemetry_sync = &m_ident ## _telemetry_sync,                                                  \
    };                                                                                                  \

#define MAGNETOMETER_SCHEDULE(m_ident)                                                                  \
    TASK_SCHEDULE(m_ident ## _query, 100)                                                               \
    TASK_SCHEDULE(m_ident ## _telem, 100)

void magnetometer_set_powered(magnetometer_t *mag, bool powered);

#endif /* FSW_MAGNETOMETER_H */