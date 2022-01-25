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
#include <stdlib.h>
#include <string.h>

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"

#include <rtos/gic.h>
#include <hal/atomic.h>

enum {
    IRQ_PHYS_TIMER = IRQ_PPI_BASE + 14,
};

#define taskFOREACH( pxTCB ) \
    for (TCB_t * pxTCB = tasktable_start; pxTCB < tasktable_end; pxTCB++)

/*-----------------------------------------------------------*/

static uint32_t schedule_index = 0;
TCB_t * volatile pxCurrentTCB = NULL;

/* Other file private variables. --------------------------------*/
static volatile BaseType_t xSchedulerRunning = pdFALSE;

/*-----------------------------------------------------------*/

// This is only called in two different circumstances:
//   1. During initialization, to set up tasks before the scheduler starts.
//   2. When restarting a task, in a critical section.
void thread_start_internal( TCB_t * pxNewTCB )
{
    assert(pxNewTCB != NULL);

    StackType_t * pxTopOfStack;

    /* Calculate the top of stack address. */
    pxTopOfStack = &( pxNewTCB->pxStack[ RTOS_STACK_SIZE - ( uint32_t ) 1 ] );
    pxTopOfStack = ( StackType_t * ) ( ( ( portPOINTER_SIZE_TYPE ) pxTopOfStack ) & ( ~( ( portPOINTER_SIZE_TYPE ) portBYTE_ALIGNMENT_MASK ) ) ); /*lint !e923 !e9033 !e9078 MISRA exception.  Avoiding casts between pointers and integers is not practical.  Size differences accounted for using portPOINTER_SIZE_TYPE type.  Checked by assert(). */

    /* Check the alignment of the calculated top of stack is correct. */
    configASSERT( ( ( ( portPOINTER_SIZE_TYPE ) pxTopOfStack & ( portPOINTER_SIZE_TYPE ) portBYTE_ALIGNMENT_MASK ) == 0UL ) );

    /* Initialize the TCB stack to look as if the task was already running,
     * but had been interrupted by the scheduler.  The return address is set
     * to the start of the task function. Once the stack has been initialised
     * the top of stack variable is updated. */
    pxNewTCB->mut->pxTopOfStack = pxPortInitialiseStack( pxTopOfStack, pxNewTCB );
}
/*-----------------------------------------------------------*/

void thread_restart_other_task(TCB_t *pxTCB) {
    assert(pxTCB != NULL);
    assert(pxTCB->restartable == RESTARTABLE);
    assert(pxTCB != pxCurrentTCB);

    debugf(WARNING, "Restarting task '%s'", pxTCB->pcTaskName);

    // this needs to be in a critical section so that there is no period of time in which other tasks could run AND
    // the TaskHandle could refer to undefined memory.
    taskENTER_CRITICAL();

    pxTCB->mut->hit_restart = true;
    thread_start_internal(pxTCB);

    taskEXIT_CRITICAL();

    debugf(WARNING, "Completed restart for task '%s'", pxTCB->pcTaskName);
}

/*-----------------------------------------------------------*/

static inline void schedule_load(bool validate)
{
    assert(schedule_index < task_scheduling_order_length);
    schedule_entry_t sched = task_scheduling_order[schedule_index];
    pxCurrentTCB = sched.task;
    assert(pxCurrentTCB != NULL && pxCurrentTCB >= tasktable_start && pxCurrentTCB < tasktable_end);

    // update the next callback time to the next timing tick
    uint64_t old_time = arm_get_cntp_cval();
    uint64_t new_time = old_time + sched.nanos / CLOCK_PERIOD_NS;
    arm_set_cntp_cval(new_time);

    if (validate) {
        uint64_t here = timer_now_ns();
        // make sure we aren't drifting from the schedule
        assert(old_time * CLOCK_PERIOD_NS <= here && here <= new_time * CLOCK_PERIOD_NS);
    }

#ifdef TASK_DEBUG
    debugf(TRACE, "FreeRTOS scheduling %15s until %" PRIu64, pxCurrentTCB->pcTaskName, new_time * CLOCK_PERIOD_NS);
#endif
}

/*-----------------------------------------------------------*/

static void unexpected_irq_callback(void *opaque)
{
    (void) opaque;
    abortf("should not have gotten a callback on the timer IRQ");
}

void vTaskStartScheduler( void )
{
    /* Interrupts are turned off here, to ensure a tick does not occur
     * before or during the call to xPortStartScheduler().  The stacks of
     * the created tasks contain a status word with interrupts switched on
     * so interrupts will automatically get re-enabled when the first task
     * starts to run. */
    portDISABLE_INTERRUPTS();

    xSchedulerRunning = pdTRUE;

    taskFOREACH(task) {
        thread_start_internal(task);
    }
    debugf(DEBUG, "Started %u pre-registered threads!", tasktable_end - tasktable_start);

    /* Start the timer that generates the tick ISR. */
    assert(TIMER_ASSUMED_CNTFRQ == arm_get_cntfrq());

    // start scheduling at next millisecond boundary (yes, this means the first task might have a bit of extra
    // time, but we can live with that)
    uint64_t start_time_ns = timer_now_ns();
    start_time_ns += CLOCK_NS_PER_MS - (start_time_ns % CLOCK_NS_PER_MS);
    arm_set_cntp_cval(start_time_ns / CLOCK_PERIOD_NS);

    // set the enable bit and don't set the mask bit
    arm_set_cntp_ctl(ARM_TIMER_ENABLE);
    // don't provide a real callback here, because it will never actually have to be called.
    enable_irq(IRQ_PHYS_TIMER, unexpected_irq_callback, NULL);

    // start executing first task
    schedule_index = 0;
    schedule_load(false);

    /* Setting up the timer tick is hardware specific and thus in the
     * portable interface. */
    xPortStartScheduler();
    abortf("should never return from xPortStartScheduler");
}
/*-----------------------------------------------------------*/

void vTaskSwitchContext( void )
{
    /* Is the currently saved stack pointer within the stack limit? */
    if( pxCurrentTCB->mut->pxTopOfStack <= pxCurrentTCB->pxStack )
    {
        abortf("STACK OVERFLOW occurred in task '%s'", pxCurrentTCB->pcTaskName);
    }

    /* Select the next task to run, round-robin-style */
    schedule_index = (schedule_index + 1) % task_scheduling_order_length;
    schedule_load(true);
}
/*-----------------------------------------------------------*/

#if ( INCLUDE_xTaskGetSchedulerState == 1 )

    BaseType_t xTaskGetSchedulerState( void )
    {
        BaseType_t xReturn;

        if( xSchedulerRunning == pdFALSE )
        {
            xReturn = taskSCHEDULER_NOT_STARTED;
        }
        else
        {
            xReturn = taskSCHEDULER_RUNNING;
        }

        return xReturn;
    }

#endif /* ( INCLUDE_xTaskGetSchedulerState == 1 ) */
