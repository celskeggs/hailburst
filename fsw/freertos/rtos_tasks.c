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

TCB_t * volatile pxCurrentTCB = NULL;

/* Other file private variables. --------------------------------*/
static volatile TickType_t xTickCount = ( TickType_t ) configINITIAL_TICK_COUNT;
static volatile BaseType_t xSchedulerRunning = pdFALSE;
static volatile BaseType_t xYieldPending = pdFALSE;

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

void vTaskStartScheduler( void )
{
    /* Interrupts are turned off here, to ensure a tick does not occur
     * before or during the call to xPortStartScheduler().  The stacks of
     * the created tasks contain a status word with interrupts switched on
     * so interrupts will automatically get re-enabled when the first task
     * starts to run. */
    portDISABLE_INTERRUPTS();

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

    if( xYieldPending != pdFALSE )
    {
        xSwitchRequired = pdTRUE;
    }

    /* Tasks will share processing time (time slice) if preemption is on. */
    if ( !xSwitchRequired )
    {
        taskFOREACH( pxTCB )
        {
            if ( pxTCB != pxCurrentTCB )
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
        if ( chooseNext == true )
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

uint32_t ulTaskNotifyTakeIndexed( UBaseType_t uxIndexToWait )
{
    uint32_t ulReturn;

    configASSERT( uxIndexToWait < configTASK_NOTIFICATION_ARRAY_ENTRIES );

    taskENTER_CRITICAL();
    {
        ulReturn = pxCurrentTCB->mut->ulNotifiedValue[ uxIndexToWait ];

        pxCurrentTCB->mut->ulNotifiedValue[ uxIndexToWait ] = 0UL;
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

    configASSERT( uxIndexToNotify < configTASK_NOTIFICATION_ARRAY_ENTRIES );
    configASSERT( xTaskToNotify );
    pxTCB = xTaskToNotify;

    taskENTER_CRITICAL();
    {
        ( pxTCB->mut->ulNotifiedValue[ uxIndexToNotify ] )++;
    }
    taskEXIT_CRITICAL();

    return xReturn;
}

/*-----------------------------------------------------------*/

void vTaskNotifyGiveIndexedFromISR( TaskHandle_t xTaskToNotify,
                                    UBaseType_t uxIndexToNotify )
{
    TCB_t * pxTCB;
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
        /* 'Giving' is equivalent to incrementing a count in a counting
         * semaphore. */
        ( pxTCB->mut->ulNotifiedValue[ uxIndexToNotify ] )++;
    }
    portCLEAR_INTERRUPT_MASK_FROM_ISR( uxSavedInterruptStatus );
}
