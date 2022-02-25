#include <hal/clip.h>
#include <synch/config.h>

void clip_loop(clip_t *clip) {
    assert(clip != NULL);

    uint32_t now;
    uint32_t current_tick = task_tick_index();

    for (;;) {
        if (current_tick != (now = task_tick_index())) {
            miscomparef("Clip %s desynched from timeline. Tick found to be %u instead of %u.",
                        clip->label, now, current_tick);
        }

        clip->clip_play(clip->clip_argument);

        if (current_tick != (now = task_tick_index())) {
            miscomparef("Clip %s overran scheduling period. Tick found to be %u instead of %u.",
                        clip->label, now, current_tick);
        }

        task_yield();

        current_tick++;
    }
}
