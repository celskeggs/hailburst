#ifndef FSW_MAGNETOMETER_H
#define FSW_MAGNETOMETER_H

#include <stdbool.h>

#include <hal/thread.h>
#include <fsw/fakewire/rmap.h>
#include <fsw/fakewire/switch.h>
#include <fsw/telemetry.h>

enum {
    MAGNETOMETER_MAX_READINGS = 100,
};

typedef struct {
    rmap_t     *endpoint;
    rmap_addr_t address;

    // synchronization
    bool should_be_powered;
    semaphore_t *flag_change;

    // telemetry buffer
    chart_t *readings;

    // telemetry output endpoint
    tlm_async_endpoint_t *telemetry_async;
    tlm_sync_endpoint_t  *telemetry_sync;
} magnetometer_t;

void magnetometer_drop_notification(void);
void magnetometer_mainloop(magnetometer_t *mag);
void magnetometer_telemloop(magnetometer_t *mag);

#define MAGNETOMETER_REGISTER(m_ident, m_address, m_receive, m_transmit)                        \
    CHART_REGISTER(m_ident ## _readings, sizeof(tlm_mag_reading_t), MAGNETOMETER_MAX_READINGS); \
    CHART_SERVER_NOTIFY(m_ident ## _readings, magnetometer_drop_notification, NULL);            \
    CHART_CLIENT_NOTIFY(m_ident ## _readings, magnetometer_drop_notification, NULL);            \
    SEMAPHORE_REGISTER(m_ident ## _flag_change);                                                \
    TELEMETRY_ASYNC_REGISTER(m_ident ## _telemetry_async);                                      \
    TELEMETRY_SYNC_REGISTER(m_ident ## _telemetry_sync);                                        \
    RMAP_REGISTER(m_ident ## _endpoint, 8, 4, m_receive, m_transmit);                           \
    magnetometer_t m_ident = {                                                                  \
        .endpoint = &m_ident ## _endpoint,                                                      \
        .address = (m_address),                                                                 \
        .should_be_powered = false,                                                             \
        .flag_change = &m_ident ## _flag_change,                                                \
        .readings = &m_ident ## _readings,                                                      \
        .telemetry_async = &m_ident ## _telemetry_async,                                        \
        .telemetry_sync = &m_ident ## _telemetry_sync,                                          \
    };                                                                                          \
    TASK_REGISTER(m_ident ## _query, "mag_query_loop", PRIORITY_WORKERS,                        \
                  magnetometer_mainloop, &m_ident, RESTARTABLE);                                \
    TASK_REGISTER(m_ident ## _telem, "mag_telem_loop", PRIORITY_WORKERS,                        \
                  magnetometer_telemloop, &m_ident, RESTARTABLE)

void magnetometer_set_powered(magnetometer_t *mag, bool powered);

#endif /* FSW_MAGNETOMETER_H */
