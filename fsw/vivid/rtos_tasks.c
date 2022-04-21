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

/* Standard includes. */
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"

#include <rtos/gic.h>
#include <hal/atomic.h>

enum {
    /* The value of the mode bits in the APSR when the CPU is executing in user mode. */
    portAPSR_USER_MODE = 0x10,
};

/*-----------------------------------------------------------*/

static uint32_t schedule_index = 0;
uint64_t schedule_loads = 0;
uint32_t schedule_ticks = 0;
local_time_t schedule_period_start = 0;
local_time_t schedule_last = 0;
local_time_t schedule_epoch_start = 0;
TCB_t * volatile pxCurrentTCB = NULL;
static StackType_t shared_clip_stack[RTOS_STACK_SIZE] __attribute__((aligned (8)));
static StackType_t * const clip_top_of_stack = &shared_clip_stack[RTOS_STACK_SIZE - 2];

/*-----------------------------------------------------------*/

__attribute__((noreturn))
static void schedule_execute(bool validate)
{
    assert(schedule_index < task_scheduling_order_length);
    schedule_entry_t sched = task_scheduling_order[schedule_index];
    pxCurrentTCB = sched.task;
    assert(pxCurrentTCB != NULL);
    schedule_loads++;

    if (schedule_index == 0) {
        schedule_epoch_start = schedule_last;
    }

    // update the next callback time to the next timing tick
    uint64_t new_time = schedule_last + sched.nanos;
    arm_set_cntp_cval(new_time / CLOCK_PERIOD_NS);
    // set the enable bit and don't set the mask bit
    arm_set_cntp_ctl(ARM_TIMER_ENABLE);

#ifdef TASK_DEBUG
    debugf(TRACE, "VIVID scheduling %15s until %" PRIu64, sched.task->pcTaskName, new_time);
#endif

    if (validate) {
        uint64_t here = timer_now_ns();
        // make sure we aren't drifting from the schedule
        assert(schedule_last <= here && here <= new_time);
    }

    // make the start of the scheduling period available to code that may be interested
    schedule_period_start = schedule_last;

    schedule_last = new_time;

    start_clip_context(clip_top_of_stack);
}

/*-----------------------------------------------------------*/

__attribute__((noreturn)) void vTaskStartScheduler( void )
{
    /* Interrupts are verified to be off here, to ensure ticks do not execute while the scheduler is being started.
     * When clips are executed, the status word will be switched such that interrupts are re-enabled. */
    uint32_t cpsr = arm_get_cpsr();
    assert((cpsr & ARM_CPSR_MASK_INTERRUPTS) != 0);
    /* Also, ensure that we are in IRQ mode, which is the standard mode for executing in the scheduler. */
    assert((cpsr & ARM_CPSR_MASK_MODE) == ARM_IRQ_MODE);

    /* Check the alignment of the calculated top of stack is correct. */
    assert(((uintptr_t) clip_top_of_stack & 0x0007) == 0UL);

    assert(pxCurrentTCB == NULL);

    /* Start the timer that generates the tick ISR. */
    assert(TIMER_ASSUMED_CNTFRQ == arm_get_cntfrq());

    // start scheduling at next millisecond boundary (yes, this means the first task might have a bit of extra
    // time, but we can live with that)
    uint64_t start_time_ns = timer_now_ns();
    schedule_last = start_time_ns + CLOCK_NS_PER_MS - (start_time_ns % CLOCK_NS_PER_MS);

    // start executing first task
    schedule_index = 0;
    schedule_execute(false);
}
/*-----------------------------------------------------------*/

__attribute__((noreturn)) void schedule_next_task( void )
{
    /* Select the next task to run, round-robin-style */
    schedule_index = (schedule_index + 1) % task_scheduling_order_length;
    if (schedule_index == 0) {
        schedule_ticks++;
    }
    schedule_execute(true);
}
