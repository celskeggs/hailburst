/*
 * This file is borrowed from FreeRTOS.
 *
 * FreeRTOS Kernel <DEVELOPMENT BRANCH>
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <rtos/config.h>
#include <rtos/gic.h>
#include <rtos/scheduler.h>
#include <hal/atomic.h>

static uint32_t schedule_index = 0;
uint64_t schedule_loads = 0;
uint32_t schedule_ticks = 0;
local_time_t schedule_period_start = 0;
local_time_t schedule_last = 0;
local_time_t schedule_epoch_start = 0;
clip_t *schedule_current_clip = NULL;

static __attribute__((noreturn)) void schedule_execute(bool validate) {
    assert(schedule_index < schedule_partitions_length);
    schedule_entry_t sched = schedule_partitions[schedule_index];
    atomic_store(schedule_current_clip, sched.clip);
    assert(schedule_current_clip != NULL);
    schedule_loads++;

    uint64_t sched_now = timer_now_ns();

#if ( VIVID_PARTITION_SCHEDULE_ENFORCEMENT == 0 )
    // if no minimum or maximum times, just say it was supposed to end now, whatever.
    schedule_last = sched_now;
#else
#if ( VIVID_PARTITION_SCHEDULE_ENFORCEMENT == 1 )
    // if no minimum times, then rewind schedule_last to the current time if necessary.
    if (sched_now < schedule_last) {
        schedule_last = sched_now;
    }
#endif
#endif

    if (schedule_index == 0) {
        schedule_epoch_start = schedule_last;
    }

    // compute the next timing tick
    uint64_t new_time = schedule_last + sched.nanos;

#ifdef TASK_DEBUG
    debugf(TRACE, "VIVID scheduling %15s until %" PRIu64, sched.clip->label, new_time);
#endif

#if ( VIVID_PARTITION_SCHEDULE_ENFORCEMENT == 0 )
    // nothing to do if there are no schedule constraints
    (void) validate;
#else
    if (validate) {
        // make sure we aren't drifting from the schedule
        assertf(schedule_last <= sched_now && sched_now <= new_time,
                "schedule invariant last=" TIMEFMT " <= here=" TIMEFMT " <= new_time=" TIMEFMT " violated",
                TIMEARG(schedule_last), TIMEARG(sched_now), TIMEARG(new_time));
    }

    // set the next callback time
    arm_set_cntp_cval(new_time / CLOCK_PERIOD_NS);
    // set the enable bit and don't set the mask bit
    arm_set_cntp_ctl(ARM_TIMER_ENABLE);

    gic_validate_ready();
#endif

    // make the start of the scheduling period available to code that may be interested
    schedule_period_start = schedule_last;

    schedule_last = new_time;

    sched.clip->enter_context();
    abortf("should never return from enter_context");
}

__attribute__((noreturn)) void schedule_first_clip(void) {
    /* Interrupts are verified to be off here, to ensure ticks do not execute while the scheduler is being started.
     * When clips are executed, the status word will be switched such that interrupts are re-enabled. */
    uint32_t cpsr = arm_get_cpsr();
    assert((cpsr & ARM_CPSR_MASK_INTERRUPTS) != 0);
    /* Also, ensure that we are in IRQ mode, which is the standard mode for executing in the scheduler. */
    assert((cpsr & ARM_CPSR_MASK_MODE) == ARM_IRQ_MODE);

    assert(schedule_current_clip == NULL);

    /* Start the timer that generates the tick ISR. */
    assert(TIMER_ASSUMED_CNTFRQ == arm_get_cntfrq());

    // start scheduling at next millisecond boundary (yes, this means the first clip might have a bit of extra
    // time, but we can live with that)
    uint64_t start_time_ns = timer_now_ns();
    schedule_last = start_time_ns + CLOCK_NS_PER_MS - (start_time_ns % CLOCK_NS_PER_MS);

    // start executing first clip
    schedule_index = 0;
    schedule_execute(false);
}

__attribute__((noreturn)) void schedule_next_clip(void) {
    /* Select the next clip to run, round-robin-style */
    schedule_index = (schedule_index + 1) % schedule_partitions_length;
    if (schedule_index == 0) {
        schedule_ticks++;
    }
    schedule_execute(true);
}
