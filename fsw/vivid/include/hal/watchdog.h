#ifndef FSW_VIVID_HAL_WATCHDOG_H
#define FSW_VIVID_HAL_WATCHDOG_H

#include <hal/init.h>
#include <synch/duct.h>

#define WATCHDOG_VOTER_REPLICAS 3

enum {
    WATCHDOG_STARTUP_GRACE_PERIOD = 1 * CLOCK_NS_PER_SEC,
};

typedef const struct {
    struct watchdog_aspect_replica_mut {
        local_time_t last_known_ok;
    } *mut;
    const char *label;
    duct_t     *duct;
    duration_t  timeout_ns;
} watchdog_aspect_replica_t;

typedef const struct {
    watchdog_aspect_replica_t replicas[WATCHDOG_VOTER_REPLICAS];
    // separate copy for the sender
    duct_t *duct;
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
    struct watchdog_voter_replica_mut {
        local_time_t init_window_end;
    } *mut;
    uint8_t             replica_id;
    watchdog_aspect_t **aspects;
    size_t              num_aspects;
    duct_t             *recipe_duct;
    duct_t             *food_duct;
} watchdog_voter_replica_t;

typedef const struct {
    duct_t *recipe_duct;
    duct_t *food_duct;
} watchdog_monitor_t;

void watchdog_voter_clip(watchdog_voter_replica_t *wvr);
void watchdog_monitor_clip(watchdog_monitor_t *wm);

macro_define(WATCHDOG_ASPECT_PROTO, a_ident) {
    extern watchdog_aspect_t a_ident
}

macro_define(WATCHDOG_ASPECT, a_ident, a_timeout_ns, a_sender_replicas) {
    DUCT_REGISTER(symbol_join(a_ident, duct), a_sender_replicas, WATCHDOG_VOTER_REPLICAS, 1, sizeof(uint8_t),
                  DUCT_SENDER_FIRST);
    static_repeat(WATCHDOG_VOTER_REPLICAS, w_replica_id) {
        struct watchdog_aspect_replica_mut symbol_join(a_ident, replica, w_replica_id) = {
            .last_known_ok = 0,
        };
    }
    watchdog_aspect_t a_ident = {
        .replicas = {
            static_repeat(WATCHDOG_VOTER_REPLICAS, w_replica_id) {
                {
                    .mut = &symbol_join(a_ident, replica, w_replica_id),
                    .label = symbol_str(a_ident),
                    .duct = &symbol_join(a_ident, duct),
                    .timeout_ns = (a_timeout_ns),
                },
            }
        },
        .duct = &symbol_join(a_ident, duct),
    }
}

macro_define(WATCHDOG_REGISTER, w_ident, w_aspects) {
    DUCT_REGISTER(symbol_join(w_ident, recipe_duct), 1, WATCHDOG_VOTER_REPLICAS,
                  1, sizeof(struct watchdog_recipe_message), DUCT_RECEIVER_FIRST);
    DUCT_REGISTER(symbol_join(w_ident, food_duct), WATCHDOG_VOTER_REPLICAS, 1,
                  1, sizeof(struct watchdog_food_message), DUCT_SENDER_FIRST);
    static_repeat(WATCHDOG_VOTER_REPLICAS, w_replica_id) {
        watchdog_aspect_t *symbol_join(w_ident, aspects, w_replica_id)[] = w_aspects;
        struct watchdog_voter_replica_mut symbol_join(w_ident, mutable, w_replica_id) = {
            .init_window_end = 0, /* populated during init */
        };
        watchdog_voter_replica_t symbol_join(w_ident, voter, w_replica_id) = {
            .mut = &symbol_join(w_ident, mutable, w_replica_id),
            .replica_id = w_replica_id,
            .aspects = symbol_join(w_ident, aspects, w_replica_id),
            .num_aspects = PP_ARRAY_SIZE(symbol_join(w_ident, aspects, w_replica_id)),
            .recipe_duct = &symbol_join(w_ident, recipe_duct),
            .food_duct = &symbol_join(w_ident, food_duct),
        };
        static inline void symbol_join(w_ident, init, w_replica_id)(void) {
            symbol_join(w_ident, voter, w_replica_id).mut->init_window_end
                    = timer_now_ns() + WATCHDOG_STARTUP_GRACE_PERIOD;
        }
        PROGRAM_INIT(STAGE_RAW, symbol_join(w_ident, init, w_replica_id));
        CLIP_REGISTER(symbol_join(w_ident, voter_clip, w_replica_id),
                      watchdog_voter_clip, &symbol_join(w_ident, voter, w_replica_id));
    }
    watchdog_monitor_t symbol_join(w_ident, monitor) = {
        .recipe_duct = &symbol_join(w_ident, recipe_duct),
        .food_duct = &symbol_join(w_ident, food_duct),
    };
    CLIP_REGISTER(symbol_join(w_ident, monitor_clip), watchdog_monitor_clip, &symbol_join(w_ident, monitor))
}

macro_define(WATCHDOG_SCHEDULE, w_ident) {
    static_repeat(WATCHDOG_VOTER_REPLICAS, w_replica_id) {
        CLIP_SCHEDULE(symbol_join(w_ident, voter_clip, w_replica_id), 30)
    }
    CLIP_SCHEDULE(symbol_join(w_ident, monitor_clip), 10)
}

// must be called every epoch
void watchdog_indicate(watchdog_aspect_t *aspect, uint8_t replica_id, bool ok);

void watchdog_force_reset(void) __attribute__((noreturn));

#endif /* FSW_VIVID_HAL_WATCHDOG_H */
