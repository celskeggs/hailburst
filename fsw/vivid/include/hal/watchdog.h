#ifndef FSW_VIVID_HAL_WATCHDOG_H
#define FSW_VIVID_HAL_WATCHDOG_H

#include <rtos/config.h>
#include <hal/init.h>
#include <synch/duct.h>

// use default number of replicas
#define WATCHDOG_VOTER_REPLICAS CONFIG_APPLICATION_REPLICAS

enum {
    WATCHDOG_STARTUP_GRACE_PERIOD = 1 * CLOCK_NS_PER_SEC,
};

#if ( VIVID_WATCHDOG_MONITOR_ASPECTS == 1 )
typedef const struct {
    struct watchdog_aspect_replica_mut {
        local_time_t last_known_ok;
    } *mut;
    const char *label;
    duct_t     *duct;
    duration_t  timeout_ns;
} watchdog_aspect_replica_t;
#endif

typedef const struct {
#if ( VIVID_WATCHDOG_MONITOR_ASPECTS == 1 )
    watchdog_aspect_replica_t replicas[WATCHDOG_VOTER_REPLICAS];
    // separate copy for the sender
    duct_t *duct;
#endif
} watchdog_aspect_t;

// only sent when it's time to decide whether to feed the watchdog.
struct watchdog_recipe_message {
    uint32_t recipe;
};

// sent in response to a recipe message OR if it's time to force-reset the watchdog. (a message is sent, instead of
// directly forcing a reset, so that voting can take place.)
struct watchdog_food_message {
    uint32_t food; // only populated if force_reset is false.
    bool force_reset;
} __attribute__((packed));
static_assert(sizeof(struct watchdog_food_message) == 5, "must not be any padding to cause memcmp issues");

typedef const struct {
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

void watchdog_populate_aspect_timeouts(watchdog_aspect_t **aspects, size_t num_aspects);
void watchdog_voter_clip(watchdog_voter_replica_t *wvr);
void watchdog_monitor_clip(watchdog_monitor_t *wm);

macro_define(WATCHDOG_ASPECT_PROTO, a_ident) {
    extern watchdog_aspect_t a_ident
}

macro_define(WATCHDOG_ASPECT, a_ident, a_timeout_ns, a_sender_replicas) {
#if ( VIVID_WATCHDOG_MONITOR_ASPECTS == 1 )
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
#else /* ( VIVID_WATCHDOG_MONITOR_ASPECTS == 0 ) */
    watchdog_aspect_t a_ident = {};
#endif
}

macro_define(WATCHDOG_REGISTER, w_ident, w_aspects) {
    DUCT_REGISTER(symbol_join(w_ident, recipe_duct), 1, WATCHDOG_VOTER_REPLICAS,
                  1, sizeof(struct watchdog_recipe_message), DUCT_RECEIVER_FIRST);
    DUCT_REGISTER(symbol_join(w_ident, food_duct), WATCHDOG_VOTER_REPLICAS, 1,
                  1, sizeof(struct watchdog_food_message), DUCT_SENDER_FIRST);
    static_repeat(WATCHDOG_VOTER_REPLICAS, w_replica_id) {
        watchdog_aspect_t *symbol_join(w_ident, aspects, w_replica_id)[] = w_aspects;
        watchdog_voter_replica_t symbol_join(w_ident, voter, w_replica_id) = {
            .replica_id = w_replica_id,
            .aspects = symbol_join(w_ident, aspects, w_replica_id),
            .num_aspects = PP_ARRAY_SIZE(symbol_join(w_ident, aspects, w_replica_id)),
            .recipe_duct = &symbol_join(w_ident, recipe_duct),
            .food_duct = &symbol_join(w_ident, food_duct),
        };
        CLIP_REGISTER(symbol_join(w_ident, voter_clip, w_replica_id),
                      watchdog_voter_clip, &symbol_join(w_ident, voter, w_replica_id));
    }
#if ( VIVID_WATCHDOG_MONITOR_ASPECTS == 1 )
    static inline void symbol_join(w_ident, init)(void) {
        watchdog_populate_aspect_timeouts(
            symbol_join(w_ident, aspects, 0),
            PP_ARRAY_SIZE(symbol_join(w_ident, aspects, 0))
        );
    }
    PROGRAM_INIT(STAGE_RAW, symbol_join(w_ident, init));
#endif
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
