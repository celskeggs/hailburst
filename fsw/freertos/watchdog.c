#include <inttypes.h>
#include <stdint.h>

#include <hal/debug.h>
#include <hal/init.h>
#include <hal/system.h>
#include <hal/thread.h>
#include <hal/timer.h>
#include <hal/watchdog.h>
#include <synch/duct.h>

enum {
    WATCHDOG_VOTER_REPLICAS = 1,
    WATCHDOG_VOTER_ID = 0,

    WATCHDOG_BASE_ADDRESS = 0x090c0000,

    WATCHDOG_ASPECT_MAX_AGE = CLOCK_NS_PER_SEC,
};

struct watchdog_mmio_region {
    uint32_t r_greet;        // read-only, variable
    uint32_t r_feed;         // write-only
    uint32_t r_deadline;     // read-only, variable
    uint32_t r_early_offset; // read-only, constant
};

bool watchdog_initialized = false;
uint64_t watchdog_init_window_end;
uint64_t watchdog_aspect_timestamps[WATCHDOG_ASPECT_NUM] = { 0 };

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

void watchdog_ok(watchdog_aspect_t aspect) {
    size_t offset = (size_t) aspect;
    assert(offset < WATCHDOG_ASPECT_NUM);
    atomic_store_relaxed(watchdog_aspect_timestamps[offset], timer_now_ns());
}

static const char *watchdog_aspect_name(watchdog_aspect_t w) {
    static_assert(WATCHDOG_ASPECT_NUM == 4, "watchdog_aspect_name should be updated alongside watchdog_aspect_t");
    assert(w < WATCHDOG_ASPECT_NUM);

    switch (w) {
    case WATCHDOG_ASPECT_RADIO_UPLINK:
        return "RADIO_UPLINK";
    case WATCHDOG_ASPECT_RADIO_DOWNLINK:
        return "RADIO_DOWNLINK";
    case WATCHDOG_ASPECT_TELEMETRY:
        return "TELEMETRY";
    case WATCHDOG_ASPECT_HEARTBEAT:
        return "HEARTBEAT";
    default:
        assert(false);
    }
}

static bool watchdog_aspects_ok(void) {
    uint64_t now = timer_now_ns();
    bool ok = true;

    // allow one time unit after init for watchdog aspects to be populated, because the init value of 0 isn't valid
    // for subsequent reboots
    if (watchdog_init_window_end > now) {
        assert(watchdog_init_window_end < now + WATCHDOG_ASPECT_MAX_AGE);
        return true;
    }

    for (watchdog_aspect_t w = 0; w < WATCHDOG_ASPECT_NUM; w++) {
        uint64_t timestamp = atomic_load_relaxed(watchdog_aspect_timestamps[w]);
        if (timestamp + WATCHDOG_ASPECT_MAX_AGE < now || timestamp > now) {
            debugf(CRITICAL, "Aspect %s not confirmed OK.", watchdog_aspect_name(w));
            ok = false;
        }
    }
    return ok;
}

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

DUCT_REGISTER(watchdog_recipe_duct, 1, WATCHDOG_VOTER_REPLICAS, 1, sizeof(struct watchdog_recipe_message),
              DUCT_RECEIVER_FIRST);
DUCT_REGISTER(watchdog_food_duct, WATCHDOG_VOTER_REPLICAS, 1, 1, sizeof(struct watchdog_food_message),
              DUCT_SENDER_FIRST);

void watchdog_voter_clip(void) {
    duct_txn_t txn;

    duct_receive_prepare(&txn, &watchdog_recipe_duct, WATCHDOG_VOTER_ID);
    struct watchdog_recipe_message recipe_msg;
    bool has_recipe_msg = (duct_receive_message(&txn, &recipe_msg, NULL) == sizeof(recipe_msg));
    duct_receive_commit(&txn);

    // TODO: change this to use ducts
    bool aspects_ok = watchdog_aspects_ok();

    duct_send_prepare(&txn, &watchdog_food_duct, WATCHDOG_VOTER_ID);
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

    debugf(DEBUG, "now=%" PRIu64 ", deadline=%+d, earliest=%+d",
           now_full, delay_until_deadline, delay_until_earliest);

    // not equivalent to 'earliest <= now' because of overflow
    return delay_until_earliest <= 0;
}

void watchdog_monitor_clip(void) {
    duct_txn_t txn;

    struct watchdog_mmio_region *mmio = (struct watchdog_mmio_region *) WATCHDOG_BASE_ADDRESS;

    duct_receive_prepare(&txn, &watchdog_food_duct, 0);
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

    duct_send_prepare(&txn, &watchdog_recipe_duct, 0);
    if (can_feed_yet) {
        struct watchdog_recipe_message recipe_msg = {
            .recipe = atomic_load_relaxed(mmio->r_greet),
        };
        duct_send_message(&txn, &recipe_msg, sizeof(recipe_msg), 0);
    }
    duct_send_commit(&txn);
}

static void watchdog_init(void) {
    assert(!watchdog_initialized);
    watchdog_initialized = true;

    watchdog_init_window_end = timer_now_ns() + WATCHDOG_ASPECT_MAX_AGE;
}

PROGRAM_INIT(STAGE_RAW, watchdog_init);

CLIP_REGISTER(watchdog_voter, watchdog_voter_clip, NULL);
CLIP_REGISTER(watchdog_monitor, watchdog_monitor_clip, NULL);

void watchdog_force_reset(void) {
    struct watchdog_mmio_region *mmio = (struct watchdog_mmio_region *) WATCHDOG_BASE_ADDRESS;

    // writes to the greet register are forbidden
    debugf(CRITICAL, "Forcing reset via watchdog.");
    atomic_store_relaxed(mmio->r_greet, 0);
    // if we continue here, something is really wrong... that should have killed the watchdog!
    abortf("Watchdog reset did not occur! aborting.");
}
