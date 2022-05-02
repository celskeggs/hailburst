#ifndef FSW_VIVID_RTOS_SCHEDULER_H
#define FSW_VIVID_RTOS_SCHEDULER_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include <rtos/config.h>
#include <hal/debug.h>

typedef struct {
    uint64_t iteration[VIVID_SCRUBBER_COPIES];
    uint8_t  max_attempts;
} scrubber_pend_t;

typedef struct {
    uint32_t recursive_exception; // must be the first member of the struct
    bool needs_start;
    bool hit_restart;

    bool            clip_running;
    uint32_t        clip_next_tick;
#if ( VIVID_RECOVERY_WAIT_FOR_SCRUBBER == 1 )
    scrubber_pend_t clip_pend;
#endif
    uint64_t        clip_max_nanos;
} clip_mut_t;

typedef const struct {
    clip_mut_t *mut;             // must be the first member of the struct

    const char *label;          // for debugging
    void      (*enter_context)(void);
    void       *start_arg;
} clip_t;

typedef struct {
    clip_t  *clip;
    uint32_t nanos;
} schedule_entry_t;

// array containing the scheduling order for these clips, defined statically using SCHEDULE_PARTITION_ORDER
extern const schedule_entry_t schedule_partitions[];
extern const uint32_t         schedule_partitions_length;

macro_define(CLIP_SCHEDULE, c_ident, c_micros) {
    { .clip = &(c_ident), .nanos = (c_micros) * 1000 },
}

macro_block_define(SCHEDULE_PARTITION_ORDER, body) {
    const schedule_entry_t schedule_partitions[] = {
        body
    };
    const uint32_t schedule_partitions_length = PP_ARRAY_SIZE(schedule_partitions);
}

extern uint64_t schedule_loads;
extern uint32_t schedule_ticks;
extern local_time_t schedule_period_start;
extern local_time_t schedule_last;
extern local_time_t schedule_epoch_start;
extern clip_t *schedule_current_clip;

static inline clip_t *schedule_get_clip(void) {
    clip_t *clip = schedule_current_clip;
    assert(clip != NULL);
    return clip;
}

static inline bool schedule_has_started(void) {
    return schedule_current_clip != NULL;
}

static inline uint32_t schedule_tick_index(void) {
    return (uint32_t) schedule_ticks;
}

static inline uint32_t schedule_remaining_ns(void) {
    return (uint32_t) (schedule_last - timer_now_ns());
}

void schedule_first_clip(void) __attribute__((noreturn));
void schedule_next_clip(void) __attribute__((noreturn));
void clip_exit_context(void) __attribute__((noreturn));

// only separate so that the special idle clip (which is normally not used) can wait instead of directly exiting
static inline __attribute__((noreturn)) void schedule_wait_for_interrupt(void) {
    assert((arm_get_cpsr() & ARM_CPSR_MASK_INTERRUPTS) == 0);
    asm volatile("WFI");
    abortf("should never return from WFI since all non-timer interrupts are masked");
}

static inline __attribute__((noreturn)) void schedule_yield(void) {
#if ( VIVID_PARTITION_SCHEDULE_ENFORCEMENT >= 2 )
    schedule_wait_for_interrupt();
#else /* ( VIVID_PARTITION_SCHEDULE_ENFORCEMENT <= 1 ) */
    clip_exit_context();
#endif
}

static inline local_time_t timer_epoch_ns(void) {
    return schedule_epoch_start;
}

#endif /* FSW_VIVID_RTOS_SCHEDULER_H */
