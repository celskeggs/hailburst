#include <inttypes.h>
#include <stdint.h>

#include <rtos/timer.h>
#include <rtos/watchdog.h>
#include <hal/thread.h>

enum {
    WATCHDOG_BASE_ADDRESS = 0x090c0000,
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

        printf("[watchdog] now=%" PRIu64 ", deadline=%+d, earliest=%+d\n",
               now_full, delay_until_deadline, delay_until_earliest);

        // if we can't feed yet, wait until we can
        if (delay_until_earliest > 0) {
            vTaskDelay(timer_ticks_until_ns(now_full + delay_until_earliest));
            // and recheck
            continue;
        }

        // greet watchdog, prepare food, feed watchdog
        uint32_t recipe = mmio->r_greet;
        uint32_t food = wdt_strict_food_from_recipe(recipe);
        printf("[watchdog] recipe: 0x%08x -> food: 0x%08x\n", recipe, food);
        mmio->r_feed = food;

        // ensure that deadline has been updated
        assert(mmio->r_deadline != deadline);
    }
}

void watchdog_init(void) {
    assert(!watchdog_initialized);
    watchdog_initialized = true;

    thread_create(&watchdog_thread, "watchdog", PRIORITY_DRIVERS, watchdog_caretaker_loop, NULL);
}
