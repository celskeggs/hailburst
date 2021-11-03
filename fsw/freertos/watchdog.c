#include <inttypes.h>
#include <stdint.h>

#include <rtos/timer.h>
#include <hal/watchdog.h>
#include <hal/thread.h>
#include <fsw/debug.h>

enum {
    WATCHDOG_BASE_ADDRESS = 0x090c0000,

    WATCHDOG_ASPECT_MAX_AGE = TIMER_NS_PER_SEC,
};

struct watchdog_mmio_region {
    // these three change constantly, so must be marked volatile
    volatile uint32_t r_greet;    // read-only
    volatile uint32_t r_feed;     // write-only
    volatile uint32_t r_deadline; // read-only
    // this one never changes, so no need to be volatile
    uint32_t r_early_offset;      // read-only
};

static thread_t watchdog_thread;
static bool watchdog_initialized = false;
static uint64_t watchdog_init_window_end;
static volatile uint64_t watchdog_aspect_timestamps[WATCHDOG_ASPECT_NUM] = { 0 };

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
    watchdog_aspect_timestamps[offset] = timer_now_ns();
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
        if (watchdog_aspect_timestamps[w] + WATCHDOG_ASPECT_MAX_AGE < now || watchdog_aspect_timestamps[w] > now) {
            debugf("[watchdog] aspect %s not confirmed ok", watchdog_aspect_name(w));
            ok = false;
        }
    }
    return ok;
}

static void *watchdog_caretaker_loop(void *opaque) {
    (void) opaque;

    struct watchdog_mmio_region *mmio = (struct watchdog_mmio_region *) WATCHDOG_BASE_ADDRESS;

    for (;;) {
        // current (untruncated) time
        uint64_t now_full = timer_now_ns();
        // find current (truncated) time
        uint32_t now = (uint32_t) now_full;
        // find next deadline
        uint32_t deadline = mmio->r_deadline;
        // how long until then?
        int32_t delay_until_deadline = deadline - now;
        // find minimum absolute time to greet
        uint32_t earliest = deadline - mmio->r_early_offset;
        // how long until then?
        int32_t delay_until_earliest = earliest - now;

        debugf("[watchdog] now=%" PRIu64 ", deadline=%+d, earliest=%+d",
               now_full, delay_until_deadline, delay_until_earliest);

        // if we can't feed yet, wait until we can
        if (delay_until_earliest > 0) {
            vTaskDelay(timer_ticks_until_ns(now_full + delay_until_earliest));
            // and recheck
            continue;
        }

        if (!watchdog_aspects_ok()) {
            // something is wrong! DO NOT FEED WATCHDOG!
            debugf("[watchdog] something is wrong");
            watchdog_force_reset();
            return NULL;
        }

        // greet watchdog, prepare food, feed watchdog
        uint32_t recipe = mmio->r_greet;
        uint32_t food = wdt_strict_food_from_recipe(recipe);
        debugf("[watchdog] recipe: 0x%08x -> food: 0x%08x", recipe, food);
        mmio->r_feed = food;

        // ensure that deadline has been updated
        assert(mmio->r_deadline != deadline);
    }
}

void watchdog_init(void) {
    assert(!watchdog_initialized);
    watchdog_initialized = true;

    watchdog_init_window_end = timer_now_ns() + WATCHDOG_ASPECT_MAX_AGE;

    thread_create(&watchdog_thread, "watchdog", PRIORITY_DRIVERS, watchdog_caretaker_loop, NULL, RESTARTABLE);
}

void watchdog_force_reset(void) {
    struct watchdog_mmio_region *mmio = (struct watchdog_mmio_region *) WATCHDOG_BASE_ADDRESS;

    // writes to the greet register are forbidden
    debugf("[watchdog] forcing reset");
    mmio->r_greet = 0;
    // if we continue here, something is really wrong... that should have killed the watchdog!
    debugf("[watchdog] reset did not occur! aborting.");
    abort();
}
