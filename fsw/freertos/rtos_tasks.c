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
#include "stack_macros.h"

/* Values that can be assigned to the ucNotifyState member of the TCB. */
#define taskNOT_WAITING_NOTIFICATION              ( ( uint8_t ) 0 ) /* Must be zero as it is the initialised value. */
#define taskWAITING_NOTIFICATION                  ( ( uint8_t ) 1 )
#define taskNOTIFICATION_RECEIVED                 ( ( uint8_t ) 2 )

/*
 * The value used to fill the stack of a task when the task is created.  This
 * is used purely for checking the high water mark for tasks.
 */
#define tskSTACK_FILL_BYTE                        ( 0xa5U )

/* If any of the following are set then task stacks are filled with a known
 * value so the high water mark can be determined.  If none of the following are
 * set then don't fill the stack so there is no unnecessary dependency on memset. */
#if ( ( configCHECK_FOR_STACK_OVERFLOW > 1 ) )
    #define tskSET_NEW_STACKS_TO_KNOWN_VALUE    1
#else
    #define tskSET_NEW_STACKS_TO_KNOWN_VALUE    0
#endif

/*-----------------------------------------------------------*/

#define taskFOREACH( pxTCB ) \
    for (TCB_t * pxTCB = tasktable_start; pxTCB < tasktable_end; pxTCB++)

/*-----------------------------------------------------------*/

/*
 * Several functions take a TaskHandle_t parameter that can optionally be NULL,
 * where NULL is used to indicate that the handle of the currently executing
 * task should be used in place of the parameter.  This macro simply checks to
 * see if the parameter is NULL and returns a pointer to the appropriate TCB.
 */
#define prvGetTCBFromHandle( pxHandle )    ( ( ( pxHandle ) == NULL ) ? pxCurrentTCB : ( pxHandle ) )

TCB_t * volatile pxCurrentTCB = NULL;

/* Other file private variables. --------------------------------*/
static volatile TickType_t xTickCount = ( TickType_t ) configINITIAL_TICK_COUNT;
static volatile BaseType_t xSchedulerRunning = pdFALSE;
static volatile BaseType_t xYieldPending = pdFALSE;
static volatile BaseType_t xNumOfOverflows = ( BaseType_t ) 0;
static volatile TickType_t xNextTaskUnblockTime = ( TickType_t ) 0U; /* Initialised to portMAX_DELAY before the scheduler starts. */

/*-----------------------------------------------------------*/

/* File private functions. --------------------------------*/

/*
 * The currently executing task is entering the Blocked state.  Add the task to
 * either the current or the overflow delayed task list.
 */
static void prvAddCurrentTaskToDelayedList( TickType_t xTicksToWait );

/*
 * Set xNextTaskUnblockTime to the time at which the next Blocked state task
 * will exit the Blocked state.
 */
static void prvResetNextTaskUnblockTime( void );

/*-----------------------------------------------------------*/

// This is only called in two different circumstances:
//   1. During initialization, to set up tasks before the scheduler starts.
//   2. When restarting a task, in a critical section.
void thread_start_internal( TCB_t * pxNewTCB )
{
    assert(pxNewTCB != NULL);

    StackType_t * pxTopOfStack;

    /* Avoid dependency on memset() if it is not required. */
    #if ( tskSET_NEW_STACKS_TO_KNOWN_VALUE == 1 )
        {
            /* Fill the stack with a known value to assist debugging. */
            ( void ) memset( pxNewTCB->pxStack, ( int ) tskSTACK_FILL_BYTE, RTOS_STACK_SIZE * sizeof( StackType_t ) );
        }
    #endif /* tskSET_NEW_STACKS_TO_KNOWN_VALUE */

    /* Calculate the top of stack address. */
    pxTopOfStack = &( pxNewTCB->pxStack[ RTOS_STACK_SIZE - ( uint32_t ) 1 ] );
    pxTopOfStack = ( StackType_t * ) ( ( ( portPOINTER_SIZE_TYPE ) pxTopOfStack ) & ( ~( ( portPOINTER_SIZE_TYPE ) portBYTE_ALIGNMENT_MASK ) ) ); /*lint !e923 !e9033 !e9078 MISRA exception.  Avoiding casts between pointers and integers is not practical.  Size differences accounted for using portPOINTER_SIZE_TYPE type.  Checked by assert(). */

    /* Check the alignment of the calculated top of stack is correct. */
    configASSERT( ( ( ( portPOINTER_SIZE_TYPE ) pxTopOfStack & ( portPOINTER_SIZE_TYPE ) portBYTE_ALIGNMENT_MASK ) == 0UL ) );

    memset( ( void * ) &( pxNewTCB->mut->ulNotifiedValue[ 0 ] ), 0x00, sizeof( pxNewTCB->mut->ulNotifiedValue ) );
    memset( ( void * ) &( pxNewTCB->mut->ucNotifyState[ 0 ] ), 0x00, sizeof( pxNewTCB->mut->ucNotifyState ) );

    /* Initialize the TCB stack to look as if the task was already running,
     * but had been interrupted by the scheduler.  The return address is set
     * to the start of the task function. Once the stack has been initialised
     * the top of stack variable is updated. */
    pxNewTCB->mut->pxTopOfStack = pxPortInitialiseStack( pxTopOfStack, pxNewTCB );

    /* Ensure interrupts don't access the task lists while the lists are being
     * updated. */
    if( pxCurrentTCB == NULL )
    {
        /* There are no other tasks, or all the other tasks are in
         * the suspended state - make this the current task. */
        pxCurrentTCB = pxNewTCB;
    }
    else
    {
        /* If the scheduler is not already running, make this task the
         * current task. */
        if( xSchedulerRunning == pdFALSE )
        {
            pxCurrentTCB = pxNewTCB;
        }
    }

    pxNewTCB->mut->task_state = TS_READY;
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

    /* Reset the next expected unblock time in case it referred to
     * the task that has just been deleted. */
    prvResetNextTaskUnblockTime();

    taskEXIT_CRITICAL();

    debugf(WARNING, "Completed restart for task '%s'", pxTCB->pcTaskName);
}

/*-----------------------------------------------------------*/

#if ( INCLUDE_xTaskDelayUntil == 1 )

    BaseType_t xTaskDelayUntil( TickType_t * const pxPreviousWakeTime,
                                const TickType_t xTimeIncrement )
    {
        TickType_t xTimeToWake;
        BaseType_t xShouldDelay = pdFALSE;

        configASSERT( pxPreviousWakeTime );
        configASSERT( ( xTimeIncrement > 0U ) );

        taskENTER_CRITICAL();
        {
            /* Minor optimisation.  The tick count cannot change in this
             * block. */
            const TickType_t xConstTickCount = xTickCount;

            /* Generate the tick time at which the task wants to wake. */
            xTimeToWake = *pxPreviousWakeTime + xTimeIncrement;

            if( xConstTickCount < *pxPreviousWakeTime )
            {
                /* The tick count has overflowed since this function was
                 * lasted called.  In this case the only time we should ever
                 * actually delay is if the wake time has also  overflowed,
                 * and the wake time is greater than the tick time.  When this
                 * is the case it is as if neither time had overflowed. */
                if( ( xTimeToWake < *pxPreviousWakeTime ) && ( xTimeToWake > xConstTickCount ) )
                {
                    xShouldDelay = pdTRUE;
                }
            }
            else
            {
                /* The tick time has not overflowed.  In this case we will
                 * delay if either the wake time has overflowed, and/or the
                 * tick time is less than the wake time. */
                if( ( xTimeToWake < *pxPreviousWakeTime ) || ( xTimeToWake > xConstTickCount ) )
                {
                    xShouldDelay = pdTRUE;
                }
            }

            /* Update the wake time ready for the next call. */
            *pxPreviousWakeTime = xTimeToWake;

            if( xShouldDelay != pdFALSE )
            {
                /* prvAddCurrentTaskToDelayedList() needs the block time, not
                 * the time to wake, so subtract the current tick count. */
                prvAddCurrentTaskToDelayedList( xTimeToWake - xConstTickCount );
            }
        }
        taskEXIT_CRITICAL();

        /* Force a reschedule if we have not already done so, we may have put ourselves to sleep. */
        portYIELD();

        return xShouldDelay;
    }

#endif /* INCLUDE_xTaskDelayUntil */
/*-----------------------------------------------------------*/

#if ( INCLUDE_vTaskDelay == 1 )

    void vTaskDelay( const TickType_t xTicksToDelay )
    {
        /* A delay time of zero just forces a reschedule. */
        if( xTicksToDelay > ( TickType_t ) 0U )
        {
            taskENTER_CRITICAL();
            {
                /* A task that is removed from the event list while the
                 * scheduler is suspended will not get placed in the ready
                 * list or removed from the blocked list until the scheduler
                 * is resumed.
                 *
                 * This task cannot be in an event list as it is the currently
                 * executing task. */
                prvAddCurrentTaskToDelayedList( xTicksToDelay );
            }
            taskEXIT_CRITICAL();
        }

        /* Force a reschedule if we have not already done so, we may have put ourselves to sleep. */
        portYIELD();
    }

#endif /* INCLUDE_vTaskDelay */
/*-----------------------------------------------------------*/

#if ( INCLUDE_vTaskSuspend == 1 )

    void vTaskSuspend( TaskHandle_t xTaskToSuspend )
    {
        TCB_t * pxTCB;

        taskENTER_CRITICAL();
        {
            /* If null is passed in here then it is the running task that is
             * being suspended. */
            pxTCB = prvGetTCBFromHandle( xTaskToSuspend );

            /* Remove task from the ready/delayed list and place in the
             * suspended list. */
            pxTCB->mut->task_state = TS_SUSPENDED;

            BaseType_t x;

            for( x = 0; x < configTASK_NOTIFICATION_ARRAY_ENTRIES; x++ )
            {
                if( pxTCB->mut->ucNotifyState[ x ] == taskWAITING_NOTIFICATION )
                {
                    /* The task was blocked to wait for a notification, but is
                     * now suspended, so no notification was received. */
                    pxTCB->mut->ucNotifyState[ x ] = taskNOT_WAITING_NOTIFICATION;
                }
            }
        }
        taskEXIT_CRITICAL();

        if( xSchedulerRunning != pdFALSE )
        {
            /* Reset the next expected unblock time in case it referred to the
             * task that is now in the Suspended state. */
            taskENTER_CRITICAL();
            {
                prvResetNextTaskUnblockTime();
            }
            taskEXIT_CRITICAL();
        }

        if( pxTCB == pxCurrentTCB )
        {
            assert(xSchedulerRunning == pdTRUE);

            /* The current task has just been suspended. */
            portYIELD();
        }
    }

#endif /* INCLUDE_vTaskSuspend */
/*-----------------------------------------------------------*/

void vTaskStartScheduler( void )
{
    /* Interrupts are turned off here, to ensure a tick does not occur
     * before or during the call to xPortStartScheduler().  The stacks of
     * the created tasks contain a status word with interrupts switched on
     * so interrupts will automatically get re-enabled when the first task
     * starts to run. */
    portDISABLE_INTERRUPTS();

    xNextTaskUnblockTime = portMAX_DELAY;
    xSchedulerRunning = pdTRUE;
    xTickCount = ( TickType_t ) configINITIAL_TICK_COUNT;

#ifdef TASK_DEBUG
    trace_task_switch(pxCurrentTCB->pcTaskName);
#endif

    /* Setting up the timer tick is hardware specific and thus in the
     * portable interface. */
    xPortStartScheduler();
    abortf("should never return from PortStartScheduler");
}
/*-----------------------------------------------------------*/

TickType_t xTaskGetTickCount( void )
{
    TickType_t xTicks;

    /* Critical section required if running on a 16 bit processor. */
    portTICK_TYPE_ENTER_CRITICAL();
    {
        xTicks = xTickCount;
    }
    portTICK_TYPE_EXIT_CRITICAL();

    return xTicks;
}
/*-----------------------------------------------------------*/

BaseType_t xTaskIncrementTick( void )
{
    BaseType_t xSwitchRequired = pdFALSE;

    /* Called by the portable layer each time a tick interrupt occurs.
     * Increments the tick then checks to see if the new tick value will cause any
     * tasks to be unblocked. */

    /* Minor optimisation.  The tick count cannot change in this
     * block. */
    const TickType_t xConstTickCount = xTickCount + ( TickType_t ) 1;

    /* Increment the RTOS tick, switching the delayed and overflowed
     * delayed lists if it wraps to 0. */
    xTickCount = xConstTickCount;

    if( xConstTickCount == ( TickType_t ) 0U ) /*lint !e774 'if' does not always evaluate to false as it is looking for an overflow. */
    {
        taskFOREACH( pxMoveTCB )
        {
            if (pxMoveTCB->mut->task_state == TS_DELAYED_OVERFLOW) {
                pxMoveTCB->mut->task_state = TS_DELAYED;
            } else if (pxMoveTCB->mut->task_state == TS_DELAYED) {
                // TODO: is this necessary?
                pxMoveTCB->mut->task_state = TS_DELAYED_OVERFLOW;
            }
        }
        xNumOfOverflows++;
        prvResetNextTaskUnblockTime();
    }

    /* See if this tick has made a timeout expire.  Tasks are stored in
     * the  queue in the order of their wake time - meaning once one task
     * has been found whose block time has not expired there is no need to
     * look any further down the list. */
    if( xConstTickCount >= xNextTaskUnblockTime )
    {
        /* If no tasks are delayed, then default to setting xNextTaskUnblockTime
         * to the maximum possible value so it is extremely unlikely that the
         * if( xTickCount >= xNextTaskUnblockTime ) test will pass next time. */
        xNextTaskUnblockTime = portMAX_DELAY;

        taskFOREACH( pxTCB )
        {
            if ( pxTCB->mut->task_state != TS_DELAYED )
            {
                /* Ignore all tasks not in DELAYED state */
            }
            else if ( xConstTickCount >= pxTCB->mut->delay_deadline )
            {
                /* Deadline has passed; place the unblocked task into the
                 * ready state. */
                pxTCB->mut->task_state = TS_READY;

                /* Preemption is on, so a context switch should be performed. */
                xSwitchRequired = pdTRUE;
            }
            else if ( xNextTaskUnblockTime > pxTCB->mut->delay_deadline )
            {
                /* Found a more proximate deadline for the next unblock time */
                xNextTaskUnblockTime = pxTCB->mut->delay_deadline;
            }
        }
    }

    if( xYieldPending != pdFALSE )
    {
        xSwitchRequired = pdTRUE;
    }

    /* Tasks will share processing time (time slice) if preemption is on. */
    if ( !xSwitchRequired )
    {
        taskFOREACH( pxTCB )
        {
            if ( pxTCB != pxCurrentTCB && pxTCB->mut->task_state == TS_READY )
            {
                xSwitchRequired = pdTRUE;
                break;
            }
        }
    }

    return xSwitchRequired;
}
/*-----------------------------------------------------------*/

void vTaskSwitchContext( void )
{
    xYieldPending = pdFALSE;
    /* Check for stack overflow, if configured. */
    taskCHECK_FOR_STACK_OVERFLOW();

    /* Select the next task to run, round-robin-style */
    bool chooseNext = true;
    TCB_t * pxNextTCB = NULL;
    taskFOREACH ( pxTCB )
    {
        if ( pxTCB->mut->task_state == TS_READY && chooseNext == true )
        {
            pxNextTCB = pxTCB;
            chooseNext = false;
        }
        if ( pxTCB == pxCurrentTCB )
        {
            chooseNext = true;
        }
    }

    /* Ensure that there is a task to run */
    /* TODO: is this correct? */
    assert(pxNextTCB != NULL);
    pxCurrentTCB = pxNextTCB;

#ifdef TASK_DEBUG
    trace_task_switch(pxCurrentTCB->pcTaskName);
#endif
}
/*-----------------------------------------------------------*/

static void prvResetNextTaskUnblockTime( void )
{
    /* If no tasks are delayed, then default to setting xNextTaskUnblockTime
     * to the maximum possible value so it is extremely unlikely that the
     * if( xTickCount >= xNextTaskUnblockTime ) test will pass next time. */
    xNextTaskUnblockTime = portMAX_DELAY;

    taskFOREACH( pxTCB )
    {
        if ( pxTCB->mut->task_state != TS_DELAYED )
        {
            /* Ignore all tasks not in DELAYED state */
        }
        else if ( xNextTaskUnblockTime > pxTCB->mut->delay_deadline )
        {
            /* Found a more proximate deadline for the next unblock time */
            xNextTaskUnblockTime = pxTCB->mut->delay_deadline;
        }
    }
}
/*-----------------------------------------------------------*/

#if ( INCLUDE_xTaskGetCurrentTaskHandle == 1 )

    TaskHandle_t xTaskGetCurrentTaskHandle( void )
    {
        TaskHandle_t xReturn;

        /* A critical section is not required as this is not called from
         * an interrupt and the current TCB will always be the same for any
         * individual execution thread. */
        xReturn = pxCurrentTCB;

        return xReturn;
    }

#endif /* ( INCLUDE_xTaskGetCurrentTaskHandle == 1 ) */
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
/*-----------------------------------------------------------*/

uint32_t ulTaskNotifyTakeIndexed( UBaseType_t uxIndexToWait,
                                  BaseType_t xClearCountOnExit,
                                  TickType_t xTicksToWait )
{
    uint32_t ulReturn;

    configASSERT( uxIndexToWait < configTASK_NOTIFICATION_ARRAY_ENTRIES );

    taskENTER_CRITICAL();
    {
        /* Only block if the notification count is not already non-zero. */
        if( pxCurrentTCB->mut->ulNotifiedValue[ uxIndexToWait ] == 0UL )
        {
            /* Mark this task as waiting for a notification. */
            pxCurrentTCB->mut->ucNotifyState[ uxIndexToWait ] = taskWAITING_NOTIFICATION;

            if( xTicksToWait > ( TickType_t ) 0 )
            {
                if( xTicksToWait == portMAX_DELAY )
                {
                    /* Add the task to the suspended task list instead of a delayed task
                     * list to ensure it is not woken by a timing event.  It will block
                     * indefinitely. */
                    pxCurrentTCB->mut->task_state = TS_SUSPENDED;
                }
                else
                {
                    prvAddCurrentTaskToDelayedList( xTicksToWait );
                }

                /* All ports are written to allow a yield in a critical
                 * section (some will yield immediately, others wait until the
                 * critical section exits) - but it is not something that
                 * application code should ever do. */
                portYIELD();
            }
        }
    }
    taskEXIT_CRITICAL();

    taskENTER_CRITICAL();
    {
        ulReturn = pxCurrentTCB->mut->ulNotifiedValue[ uxIndexToWait ];

        if( ulReturn != 0UL )
        {
            if( xClearCountOnExit != pdFALSE )
            {
                pxCurrentTCB->mut->ulNotifiedValue[ uxIndexToWait ] = 0UL;
            }
            else
            {
                pxCurrentTCB->mut->ulNotifiedValue[ uxIndexToWait ] = ulReturn - ( uint32_t ) 1;
            }
        }

        pxCurrentTCB->mut->ucNotifyState[ uxIndexToWait ] = taskNOT_WAITING_NOTIFICATION;
    }
    taskEXIT_CRITICAL();

    return ulReturn;
}

/*-----------------------------------------------------------*/

BaseType_t xTaskNotifyGiveIndexed( TaskHandle_t xTaskToNotify,
                                   UBaseType_t uxIndexToNotify )
{
    TCB_t * pxTCB;
    BaseType_t xReturn = pdPASS;
    uint8_t ucOriginalNotifyState;

    configASSERT( uxIndexToNotify < configTASK_NOTIFICATION_ARRAY_ENTRIES );
    configASSERT( xTaskToNotify );
    pxTCB = xTaskToNotify;

    taskENTER_CRITICAL();
    {
        ucOriginalNotifyState = pxTCB->mut->ucNotifyState[ uxIndexToNotify ];

        pxTCB->mut->ucNotifyState[ uxIndexToNotify ] = taskNOTIFICATION_RECEIVED;

        ( pxTCB->mut->ulNotifiedValue[ uxIndexToNotify ] )++;

        /* If the task is in the blocked state specifically to wait for a
         * notification then unblock it now. */
        if( ucOriginalNotifyState == taskWAITING_NOTIFICATION )
        {
            pxTCB->mut->task_state = TS_READY;
        }
    }
    taskEXIT_CRITICAL();

    return xReturn;
}

/*-----------------------------------------------------------*/

void vTaskNotifyGiveIndexedFromISR( TaskHandle_t xTaskToNotify,
                                    UBaseType_t uxIndexToNotify )
{
    TCB_t * pxTCB;
    uint8_t ucOriginalNotifyState;
    UBaseType_t uxSavedInterruptStatus;

    configASSERT( xTaskToNotify );
    configASSERT( uxIndexToNotify < configTASK_NOTIFICATION_ARRAY_ENTRIES );

    /* RTOS ports that support interrupt nesting have the concept of a
     * maximum  system call (or maximum API call) interrupt priority.
     * Interrupts that are  above the maximum system call priority are keep
     * permanently enabled, even when the RTOS kernel is in a critical section,
     * but cannot make any calls to FreeRTOS API functions.  If configASSERT()
     * is defined in FreeRTOSConfig.h then
     * portASSERT_IF_INTERRUPT_PRIORITY_INVALID() will result in an assertion
     * failure if a FreeRTOS API function is called from an interrupt that has
     * been assigned a priority above the configured maximum system call
     * priority.  Only FreeRTOS functions that end in FromISR can be called
     * from interrupts  that have been assigned a priority at or (logically)
     * below the maximum system call interrupt priority.  FreeRTOS maintains a
     * separate interrupt safe API to ensure interrupt entry is as fast and as
     * simple as possible.  More information (albeit Cortex-M specific) is
     * provided on the following link:
     * https://www.FreeRTOS.org/RTOS-Cortex-M3-M4.html */
    portASSERT_IF_INTERRUPT_PRIORITY_INVALID();

    pxTCB = xTaskToNotify;

    uxSavedInterruptStatus = portSET_INTERRUPT_MASK_FROM_ISR();
    {
        ucOriginalNotifyState = pxTCB->mut->ucNotifyState[ uxIndexToNotify ];
        pxTCB->mut->ucNotifyState[ uxIndexToNotify ] = taskNOTIFICATION_RECEIVED;

        /* 'Giving' is equivalent to incrementing a count in a counting
         * semaphore. */
        ( pxTCB->mut->ulNotifiedValue[ uxIndexToNotify ] )++;

        /* If the task is in the blocked state specifically to wait for a
         * notification then unblock it now. */
        if( ucOriginalNotifyState == taskWAITING_NOTIFICATION )
        {
            pxTCB->mut->task_state = TS_READY;
        }
    }
    portCLEAR_INTERRUPT_MASK_FROM_ISR( uxSavedInterruptStatus );
}

/*-----------------------------------------------------------*/

static void prvAddCurrentTaskToDelayedList( TickType_t xTicksToWait )
{
    TickType_t xTimeToWake;
    const TickType_t xConstTickCount = xTickCount;

    /* Calculate the time at which the task should be woken if the event
     * does not occur.  This may overflow but this doesn't matter, the
     * kernel will manage it correctly. */
    xTimeToWake = xConstTickCount + xTicksToWait;

    pxCurrentTCB->mut->delay_deadline = xTimeToWake;

    if( xTimeToWake < xConstTickCount )
    {
        /* Wake time has overflowed.  Place this item in the overflow
         * list. */
        pxCurrentTCB->mut->task_state = TS_DELAYED_OVERFLOW;
    }
    else
    {
        /* The wake time has not overflowed, so the current block list
         * is used. */
        pxCurrentTCB->mut->task_state = TS_DELAYED;

        /* If the task entering the blocked state was placed at the
         * head of the list of blocked tasks then xNextTaskUnblockTime
         * needs to be updated too. */
        if( xTimeToWake < xNextTaskUnblockTime )
        {
            xNextTaskUnblockTime = xTimeToWake;
        }
    }
}
