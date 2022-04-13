#include <inttypes.h>
#include <stdint.h>

#include <hal/debug.h>
#include <hal/system.h>
#include <hal/thread.h>
#include <hal/timer.h>
#include <hal/watchdog.h>
#include <synch/duct.h>

enum {
    WATCHDOG_BASE_ADDRESS = 0x090c0000,
};

struct watchdog_mmio_region {
    uint32_t r_greet;        // read-only, variable
    uint32_t r_feed;         // write-only
    uint32_t r_deadline;     // read-only, variable
    uint32_t r_early_offset; // read-only, constant
};

/************** BEGIN WATCHDOG FOOD PREPARATION CODE FROM QEMU IMPLEMENTATION **************/
static uint32_t integer_power_truncated(uint32_t base, uint16_t power)
{
    uint32_t out = 1;

    for (int i = 15; i >= 0; i--) {
        out *= out;
        if (power & (1 << i)) {
            out *= base;
        }
    }

    return out;
}

static uint32_t wdt_strict_food_from_recipe(uint32_t recipe)
{
    // pick out a base and exponent from the recipe and raise the base to that power
    // (but make sure the base is odd, because if it's even, it will quickly become 0)
    uint32_t result = integer_power_truncated((recipe >> 8) | 1, recipe & 0xFFFF);
    // XOR by reversed bits
    for (int i = 0; i < 32; i++) {
        result ^= ((recipe >> i) & 1) << (31 - i);
    }
    return result;
}
/*************** END WATCHDOG FOOD PREPARATION CODE FROM QEMU IMPLEMENTATION ***************/

void watchdog_indicate(watchdog_aspect_t *aspect, uint8_t replica_id, bool ok) {
    assert(aspect != NULL);
    duct_txn_t txn;
    duct_send_prepare(&txn, aspect->duct, replica_id);
    uint8_t ok_byte = (ok ? 1 : 0);
    duct_send_message(&txn, &ok_byte, sizeof(ok_byte), 0);
    duct_send_commit(&txn);
}

void watchdog_populate_aspect_timeouts(watchdog_aspect_t **aspects, size_t num_aspects) {
    local_time_t now = timer_now_ns();

    // allow one timeout period after init for watchdog aspects to be populated, so that nothing fails immediately.
    for (size_t i = 0; i < num_aspects; i++) {
        for (size_t j = 0; j < WATCHDOG_VOTER_REPLICAS; j++) {
            watchdog_aspect_replica_t *aspect = &aspects[i]->replicas[j];
            assert(aspect != NULL);
            aspect->mut->last_known_ok = now;
        }
    }
}

static bool watchdog_aspects_ok(watchdog_voter_replica_t *w) {
    local_time_t now = timer_epoch_ns();

    bool all_ok = true;
    duct_txn_t txn;

    for (size_t i = 0; i < w->num_aspects; i++) {
        watchdog_aspect_replica_t *aspect = &w->aspects[i]->replicas[w->replica_id];
        assert(aspect != NULL);
        duct_receive_prepare(&txn, aspect->duct, w->replica_id);
        uint8_t ok_byte = 0;
        if (duct_receive_message(&txn, &ok_byte, NULL) == sizeof(ok_byte) && ok_byte == 1) {
            aspect->mut->last_known_ok = now;
        } else if (now < aspect->mut->last_known_ok || now > aspect->mut->last_known_ok + aspect->timeout_ns) {
            debugf(CRITICAL, "Aspect %s not confirmed OK.", aspect->label);
            all_ok = false;
        }
        duct_receive_commit(&txn);
    }

    return all_ok;
}

void watchdog_voter_clip(watchdog_voter_replica_t *w) {
    duct_txn_t txn;

    duct_receive_prepare(&txn, w->recipe_duct, w->replica_id);
    struct watchdog_recipe_message recipe_msg;
    bool has_recipe_msg = (duct_receive_message(&txn, &recipe_msg, NULL) == sizeof(recipe_msg));
    duct_receive_commit(&txn);

    bool aspects_ok = watchdog_aspects_ok(w);

    duct_send_prepare(&txn, w->food_duct, w->replica_id);
    if (!aspects_ok) {
        struct watchdog_food_message food_msg = {
            .force_reset = true,
        };
        duct_send_message(&txn, &food_msg, sizeof(food_msg), 0);
    } else if (has_recipe_msg) {
        struct watchdog_food_message food_msg = {
            .force_reset = false,
            .food = wdt_strict_food_from_recipe(recipe_msg.recipe),
        };
        debugf(TRACE, "Watchdog recipe: 0x%08x -> food: 0x%08x", recipe_msg.recipe, food_msg.food);
        duct_send_message(&txn, &food_msg, sizeof(food_msg), 0);
    }
    duct_send_commit(&txn);
}

static bool watchdog_check_can_feed_yet(struct watchdog_mmio_region *mmio) {
    // current (untruncated) time
    uint64_t now_full = timer_now_ns();
    // find current (truncated) time
    uint32_t now = (uint32_t) now_full;
    // find next deadline
    uint32_t deadline = atomic_load_relaxed(mmio->r_deadline);
    // how long until then?
    int32_t delay_until_deadline = deadline - now;
    // find minimum absolute time to greet
    uint32_t earliest = deadline - atomic_load_relaxed(mmio->r_early_offset);
    // how long until then?
    int32_t delay_until_earliest = earliest - now;

    debugf(TRACE, "Watchdog status: now=%" PRIu64 ", deadline=%+d, earliest=%+d.",
           now_full, delay_until_deadline, delay_until_earliest);

    // not equivalent to 'earliest <= now' because of overflow
    return delay_until_earliest <= 0;
}

void watchdog_monitor_clip(watchdog_monitor_t *w) {
    duct_txn_t txn;

    struct watchdog_mmio_region *mmio = (struct watchdog_mmio_region *) WATCHDOG_BASE_ADDRESS;

    duct_receive_prepare(&txn, w->food_duct, 0);
    struct watchdog_food_message food_msg;
    bool has_food_msg = (duct_receive_message(&txn, &food_msg, NULL) == sizeof(food_msg));
    duct_receive_commit(&txn);

    bool can_feed_yet = watchdog_check_can_feed_yet(mmio);

    if (has_food_msg) {
        if (food_msg.force_reset) {
            debugf(CRITICAL, "Watchdog voter voted to force reset.");
            watchdog_force_reset();
        } else if (!can_feed_yet) {
            debugf(WARNING, "Watchdog voter suggested feeding watchdog before the right time!");
        } else {
            debugf(TRACE, "Watchdog voter voted to feed watchdog with food: 0x%08x.", food_msg.food);
            uint32_t old_deadline = atomic_load_relaxed(mmio->r_deadline);
            atomic_store_relaxed(mmio->r_feed, food_msg.food);
            assert(atomic_load_relaxed(mmio->r_deadline) != old_deadline);
            // don't send the recipe again if we just fed the watchdog
            can_feed_yet = false;
        }
    }

    duct_send_prepare(&txn, w->recipe_duct, 0);
    if (can_feed_yet) {
        struct watchdog_recipe_message recipe_msg = {
            .recipe = atomic_load_relaxed(mmio->r_greet),
        };
        duct_send_message(&txn, &recipe_msg, sizeof(recipe_msg), 0);
    }
    duct_send_commit(&txn);
}

void watchdog_force_reset(void) {
    struct watchdog_mmio_region *mmio = (struct watchdog_mmio_region *) WATCHDOG_BASE_ADDRESS;

    // writes to the greet register are forbidden
    debugf(CRITICAL, "Forcing reset via watchdog.");
    atomic_store_relaxed(mmio->r_greet, 0);
    // if we continue here, something is really wrong... that should have killed the watchdog!
    abortf("Watchdog reset did not occur! aborting.");
}
