#ifndef FSW_FREERTOS_HAL_WATCHDOG_H
#define FSW_FREERTOS_HAL_WATCHDOG_H

#include <hal/init.h>
#include <synch/duct.h>

enum {
    WATCHDOG_VOTER_REPLICAS = 1,
    WATCHDOG_VOTER_ID = 0,

    WATCHDOG_ASPECT_MAX_AGE = 1 * CLOCK_NS_PER_SEC,
};

typedef const struct {
    const char   *label;
    duct_t       *duct;
    local_time_t *last_known_ok;
} watchdog_aspect_t;

// only sent when it's time to decide whether to feed the watchdog.
struct watchdog_recipe_message {
    uint32_t recipe;
};

// sent in response to a recipe message OR if it's time to force-reset the watchdog. (a message is sent, instead of
// directly forcing a reset, so that voting can take place.)
struct watchdog_food_message {
    bool force_reset;
    uint32_t food; // only populated if force_reset is false.
};

typedef const struct {
    struct watchdog_mut {
        local_time_t init_window_end;
    } *mut;
    watchdog_aspect_t **aspects;
    size_t              num_aspects;
    duct_t             *recipe_duct;
    duct_t             *food_duct;
} watchdog_t;

void watchdog_voter_clip(watchdog_t *w);
void watchdog_monitor_clip(watchdog_t *w);

macro_define(WATCHDOG_ASPECT_PROTO, a_ident) {
    extern watchdog_aspect_t a_ident
}

macro_define(WATCHDOG_ASPECT, a_ident, a_sender_replicas) {
    DUCT_REGISTER(symbol_join(a_ident, duct), a_sender_replicas, WATCHDOG_VOTER_REPLICAS, 1, sizeof(uint8_t),
                  DUCT_SENDER_FIRST);
    local_time_t symbol_join(a_ident, last_known_ok)[WATCHDOG_VOTER_REPLICAS];
    watchdog_aspect_t a_ident = {
        .label = symbol_str(a_ident),
        .duct = &symbol_join(a_ident, duct),
        .last_known_ok = symbol_join(a_ident, last_known_ok),
    }
}

macro_define(WATCHDOG_REGISTER, w_ident, w_aspects) {
    watchdog_aspect_t *symbol_join(w_ident, aspects)[] = w_aspects;
    DUCT_REGISTER(symbol_join(w_ident, recipe_duct), 1, WATCHDOG_VOTER_REPLICAS,
                  1, sizeof(struct watchdog_recipe_message), DUCT_RECEIVER_FIRST);
    DUCT_REGISTER(symbol_join(w_ident, food_duct), WATCHDOG_VOTER_REPLICAS, 1,
                  1, sizeof(struct watchdog_food_message), DUCT_SENDER_FIRST);
    struct watchdog_mut symbol_join(w_ident, mut) = {
        .init_window_end = 0, /* populated during init */
    };
    watchdog_t w_ident = {
        .mut = &symbol_join(w_ident, mut),
        .aspects = symbol_join(w_ident, aspects),
        .num_aspects = sizeof(symbol_join(w_ident, aspects)) / sizeof(symbol_join(w_ident, aspects)[0]),
        .recipe_duct = &symbol_join(w_ident, recipe_duct),
        .food_duct = &symbol_join(w_ident, food_duct),
    };
    static inline void symbol_join(w_ident, init)(void) {
        w_ident.mut->init_window_end = timer_now_ns() + WATCHDOG_ASPECT_MAX_AGE;
    }
    PROGRAM_INIT(STAGE_RAW, symbol_join(w_ident, init));
    CLIP_REGISTER(symbol_join(w_ident, voter),   watchdog_voter_clip, &w_ident);
    CLIP_REGISTER(symbol_join(w_ident, monitor), watchdog_monitor_clip, &w_ident)
}

macro_define(WATCHDOG_SCHEDULE, w_ident) {
    CLIP_SCHEDULE(symbol_join(w_ident, voter),   10)
    CLIP_SCHEDULE(symbol_join(w_ident, monitor), 10)
}

// must be called every epoch
void watchdog_indicate(watchdog_aspect_t *aspect, uint8_t replica_id, bool ok);

void watchdog_force_reset(void) __attribute__((noreturn));

#endif /* FSW_FREERTOS_HAL_WATCHDOG_H */
