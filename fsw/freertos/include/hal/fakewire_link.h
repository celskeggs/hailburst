#ifndef FSW_FREERTOS_HAL_FAKEWIRE_LINK_H
#define FSW_FREERTOS_HAL_FAKEWIRE_LINK_H

#include <hal/thread.h>
#include <fsw/fakewire/codec.h>

typedef struct {
    fw_link_options_t options;
    chart_t *data_rx;
    chart_t *data_tx;
} fw_link_t;

void fakewire_link_init_internal(fw_link_t *link);

#define FAKEWIRE_LINK_REGISTER(l_ident, l_options, l_rx, l_tx)                      \
    fw_link_t l_ident = {                                                           \
        .options = (l_options),                                                     \
        .data_rx = &(l_rx),                                                         \
        .data_tx = &(l_tx),                                                         \
    };                                                                              \
    PROGRAM_INIT_PARAM(STAGE_READY, fakewire_link_init_internal, l_ident, &l_ident)

#endif /* FSW_FREERTOS_HAL_FAKEWIRE_LINK_H */
