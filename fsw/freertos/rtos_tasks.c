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

#define taskYIELD_IF_USING_PREEMPTION()    portYIELD_WITHIN_API()

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

#if ( configUSE_PORT_OPTIMISED_TASK_SELECTION == 0 )

/* If configUSE_PORT_OPTIMISED_TASK_SELECTION is 0 then task selection is
 * performed in a generic way that is not optimised to any particular
 * microcontroller architecture. */

/* uxTopReadyPriority holds the priority of the highest priority ready
 * state task. */
    #define taskRECORD_READY_PRIORITY( uxPriority ) \
    {                                               \
        if( ( uxPriority ) > uxTopReadyPriority )   \
        {                                           \
            uxTopReadyPriority = ( uxPriority );    \
        }                                           \
    } /* taskRECORD_READY_PRIORITY */

/*-----------------------------------------------------------*/

    #define taskSELECT_HIGHEST_PRIORITY_TASK()                                \
    {                                                                         \
        UBaseType_t uxTopPriority = uxTopReadyPriority;                       \
                                                                              \
        /* Find the highest priority queue that contains ready tasks. */      \
        while( listLIST_IS_EMPTY( &( pxReadyTasksLists[ uxTopPriority ] ) ) ) \
        {                                                                     \
            configASSERT( uxTopPriority );                                    \
            --uxTopPriority;                                                  \
        }                                                                     \
                                                                              \
        /* listGET_OWNER_OF_NEXT_ENTRY indexes through the list, so the tasks of \
         * the  same priority get an equal share of the processor time. */                    \
        listGET_OWNER_OF_NEXT_ENTRY( pxCurrentTCB, &( pxReadyTasksLists[ uxTopPriority ] ) ); \
        uxTopReadyPriority = uxTopPriority;                                                   \
    } /* taskSELECT_HIGHEST_PRIORITY_TASK */

/*-----------------------------------------------------------*/

/* Define away taskRESET_READY_PRIORITY() and portRESET_READY_PRIORITY() as
 * they are only required when a port optimised method of task selection is
 * being used. */
    #define taskRESET_READY_PRIORITY( uxPriority )
    #define portRESET_READY_PRIORITY( uxPriority, uxTopReadyPriority )

#else /* configUSE_PORT_OPTIMISED_TASK_SELECTION */

/* If configUSE_PORT_OPTIMISED_TASK_SELECTION is 1 then task selection is
 * performed in a way that is tailored to the particular microcontroller
 * architecture being used. */

/* A port optimised version is provided.  Call the port defined macros. */
    #define taskRECORD_READY_PRIORITY( uxPriority )    portRECORD_READY_PRIORITY( uxPriority, uxTopReadyPriority )

/*-----------------------------------------------------------*/

    #define taskSELECT_HIGHEST_PRIORITY_TASK()                                                  \
    {                                                                                           \
        UBaseType_t uxTopPriority;                                                              \
                                                                                                \
        /* Find the highest priority list that contains ready tasks. */                         \
        portGET_HIGHEST_PRIORITY( uxTopPriority, uxTopReadyPriority );                          \
        configASSERT( listCURRENT_LIST_LENGTH( &( pxReadyTasksLists[ uxTopPriority ] ) ) > 0 ); \
        listGET_OWNER_OF_NEXT_ENTRY( pxCurrentTCB, &( pxReadyTasksLists[ uxTopPriority ] ) );   \
    } /* taskSELECT_HIGHEST_PRIORITY_TASK() */

/*-----------------------------------------------------------*/

/* A port optimised version is provided, call it only if the TCB being reset
 * is being referenced from a ready list.  If it is referenced from a delayed
 * or suspended list then it won't be in a ready list. */
    #define taskRESET_READY_PRIORITY( uxPriority )                                                     \
    {                                                                                                  \
        if( listCURRENT_LIST_LENGTH( &( pxReadyTasksLists[ ( uxPriority ) ] ) ) == ( UBaseType_t ) 0 ) \
        {                                                                                              \
            portRESET_READY_PRIORITY( ( uxPriority ), ( uxTopReadyPriority ) );                        \
        }                                                                                              \
    }

#endif /* configUSE_PORT_OPTIMISED_TASK_SELECTION */

/*-----------------------------------------------------------*/

/* pxDelayedTaskList and pxOverflowDelayedTaskList are switched when the tick
 * count overflows. */
#define taskSWITCH_DELAYED_LISTS()                                                \
    {                                                                             \
        List_t * pxTemp;                                                          \
                                                                                  \
        /* The delayed tasks list should be empty when the lists are switched. */ \
        configASSERT( ( listLIST_IS_EMPTY( pxDelayedTaskList ) ) );               \
                                                                                  \
        pxTemp = pxDelayedTaskList;                                               \
        pxDelayedTaskList = pxOverflowDelayedTaskList;                            \
        pxOverflowDelayedTaskList = pxTemp;                                       \
        xNumOfOverflows++;                                                        \
        prvResetNextTaskUnblockTime();                                            \
    }

/*-----------------------------------------------------------*/

/*
 * Place the task represented by pxTCB into the appropriate ready list for
 * the task.  It is inserted at the end of the list.
 */
#define prvAddTaskToReadyList( pxTCB )                                                                      \
    traceMOVED_TASK_TO_READY_STATE( pxTCB );                                                                \
    taskRECORD_READY_PRIORITY( ( pxTCB )->uxPriority );                                                     \
    listINSERT_END( &( pxReadyTasksLists[ ( pxTCB )->uxPriority ] ), &( ( pxTCB )->mut->xStateListItem ) ); \
    tracePOST_MOVED_TASK_TO_READY_STATE( pxTCB )
/*-----------------------------------------------------------*/

/*
 * Several functions take a TaskHandle_t parameter that can optionally be NULL,
 * where NULL is used to indicate that the handle of the currently executing
 * task should be used in place of the parameter.  This macro simply checks to
 * see if the parameter is NULL and returns a pointer to the appropriate TCB.
 */
#define prvGetTCBFromHandle( pxHandle )    ( ( ( pxHandle ) == NULL ) ? pxCurrentTCB : ( pxHandle ) )

TCB_t * volatile pxCurrentTCB = NULL;

/* Lists for ready and blocked tasks. --------------------
 * xDelayedTaskList1 and xDelayedTaskList2 could be moved to function scope but
 * doing so breaks some kernel aware debuggers and debuggers that rely on removing
 * the static qualifier. */
static List_t pxReadyTasksLists[ configMAX_PRIORITIES ]; /*< Prioritised ready tasks. */
static List_t xDelayedTaskList1;                         /*< Delayed tasks. */
static List_t xDelayedTaskList2;                         /*< Delayed tasks (two lists are used - one for delays that have overflowed the current tick count. */
static List_t * volatile pxDelayedTaskList;              /*< Points to the delayed task list currently being used. */
static List_t * volatile pxOverflowDelayedTaskList;      /*< Points to the delayed task list currently being used to hold tasks that have overflowed the current tick count. */

#if ( INCLUDE_vTaskSuspend == 1 )

    static List_t xSuspendedTaskList; /*< Tasks that are currently suspended. */

#endif

/* Other file private variables. --------------------------------*/
static volatile UBaseType_t uxCurrentNumberOfTasks = ( UBaseType_t ) 0U;
static volatile TickType_t xTickCount = ( TickType_t ) configINITIAL_TICK_COUNT;
static volatile UBaseType_t uxTopReadyPriority = tskIDLE_PRIORITY;
static volatile BaseType_t xSchedulerRunning = pdFALSE;
static volatile BaseType_t xYieldPending = pdFALSE;
static volatile BaseType_t xNumOfOverflows = ( BaseType_t ) 0;
static volatile TickType_t xNextTaskUnblockTime = ( TickType_t ) 0U; /* Initialised to portMAX_DELAY before the scheduler starts. */

/*-----------------------------------------------------------*/

/* File private functions. --------------------------------*/

/*
 * Utility to ready all the lists used by the scheduler.  This is called
 * automatically upon the creation of the first task.
 */
static void prvInitialiseTaskLists( void );

/*
 * The currently executing task is entering the Blocked state.  Add the task to
 * either the current or the overflow delayed task list.
 */
static void prvAddCurrentTaskToDelayedList( TickType_t xTicksToWait,
                                            const BaseType_t xCanBlockIndefinitely );

/*
 * Set xNextTaskUnblockTime to the time at which the next Blocked state task
 * will exit the Blocked state.
 */
static void prvResetNextTaskUnblockTime( void );

/*
 * Called after a new task has been created and initialised to place the task
 * under the control of the scheduler.
 */
static void prvAddNewTaskToReadyList( TCB_t * pxNewTCB );

/*-----------------------------------------------------------*/

// called from thread.c
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

    /* This is used as an array index so must ensure it's not too large. */
    configASSERT( pxNewTCB->uxPriority < configMAX_PRIORITIES );

    vListInitialiseItem( &( pxNewTCB->mut->xStateListItem ) );

    /* Set the pxNewTCB as a link back from the ListItem_t.  This is so we can get
     * back to  the containing TCB from a generic item in a list. */
    listSET_LIST_ITEM_OWNER( &( pxNewTCB->mut->xStateListItem ), pxNewTCB );

    memset( ( void * ) &( pxNewTCB->mut->ulNotifiedValue[ 0 ] ), 0x00, sizeof( pxNewTCB->mut->ulNotifiedValue ) );
    memset( ( void * ) &( pxNewTCB->mut->ucNotifyState[ 0 ] ), 0x00, sizeof( pxNewTCB->mut->ucNotifyState ) );

    /* Initialize the TCB stack to look as if the task was already running,
     * but had been interrupted by the scheduler.  The return address is set
     * to the start of the task function. Once the stack has been initialised
     * the top of stack variable is updated. */
    pxNewTCB->mut->pxTopOfStack = pxPortInitialiseStack( pxTopOfStack, pxNewTCB );

    prvAddNewTaskToReadyList(pxNewTCB);
}
/*-----------------------------------------------------------*/

static void prvAddNewTaskToReadyList( TCB_t * pxNewTCB )
{
    /* Ensure interrupts don't access the task lists while the lists are being
     * updated. */
    taskENTER_CRITICAL();
    {
        uxCurrentNumberOfTasks++;

        if( pxCurrentTCB == NULL )
        {
            /* There are no other tasks, or all the other tasks are in
             * the suspended state - make this the current task. */
            pxCurrentTCB = pxNewTCB;

            if( uxCurrentNumberOfTasks == ( UBaseType_t ) 1 )
            {
                /* This is the first task to be created so do the preliminary
                 * initialisation required.  We will not recover if this call
                 * fails, but we will report the failure. */
                prvInitialiseTaskLists();
            }
        }
        else
        {
            /* If the scheduler is not already running, make this task the
             * current task if it is the highest priority task to be created
             * so far. */
            if( xSchedulerRunning == pdFALSE )
            {
                if( pxCurrentTCB->uxPriority <= pxNewTCB->uxPriority )
                {
                    pxCurrentTCB = pxNewTCB;
                }
            }
        }

        traceTASK_CREATE( pxNewTCB );

        prvAddTaskToReadyList( pxNewTCB );
    }
    taskEXIT_CRITICAL();

    if( xSchedulerRunning != pdFALSE )
    {
        /* If the created task is of a higher priority than the current task
         * then it should run now. */
        if( pxCurrentTCB->uxPriority < pxNewTCB->uxPriority )
        {
            taskYIELD_IF_USING_PREEMPTION();
        }
    }
}
/*-----------------------------------------------------------*/

#if ( INCLUDE_vTaskDelete == 1 )

    void vTaskDelete( TaskHandle_t xTaskToDelete )
    {
        TCB_t * pxTCB;

        taskENTER_CRITICAL();
        {
            /* If null is passed in here then it is the calling task that is
             * being deleted. */
            pxTCB = prvGetTCBFromHandle( xTaskToDelete );

            /* Remove task from the ready/delayed list. */
            if( uxListRemove( &( pxTCB->mut->xStateListItem ) ) == ( UBaseType_t ) 0 )
            {
                taskRESET_READY_PRIORITY( pxTCB->uxPriority );
            }

            --uxCurrentNumberOfTasks;

            traceTASK_DELETE( pxTCB );

            /* Reset the next expected unblock time in case it referred to
             * the task that has just been deleted. */
            prvResetNextTaskUnblockTime();
        }
        taskEXIT_CRITICAL();

        /* Force a reschedule if it is the currently running task that has just
         * been deleted. */
        if( xSchedulerRunning != pdFALSE )
        {
            if( pxTCB == pxCurrentTCB )
            {
                portYIELD_WITHIN_API();
            }
        }
    }

#endif /* INCLUDE_vTaskDelete */
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
                traceTASK_DELAY_UNTIL( xTimeToWake );

                /* prvAddCurrentTaskToDelayedList() needs the block time, not
                 * the time to wake, so subtract the current tick count. */
                prvAddCurrentTaskToDelayedList( xTimeToWake - xConstTickCount, pdFALSE );
            }
        }
        taskEXIT_CRITICAL();

        /* Force a reschedule if we have not already done so, we may have put ourselves to sleep. */
        portYIELD_WITHIN_API();

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
                traceTASK_DELAY();

                /* A task that is removed from the event list while the
                 * scheduler is suspended will not get placed in the ready
                 * list or removed from the blocked list until the scheduler
                 * is resumed.
                 *
                 * This task cannot be in an event list as it is the currently
                 * executing task. */
                prvAddCurrentTaskToDelayedList( xTicksToDelay, pdFALSE );
            }
            taskEXIT_CRITICAL();
        }

        /* Force a reschedule if we have not already done so, we may have put ourselves to sleep. */
        portYIELD_WITHIN_API();
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

            traceTASK_SUSPEND( pxTCB );

            /* Remove task from the ready/delayed list and place in the
             * suspended list. */
            if( uxListRemove( &( pxTCB->mut->xStateListItem ) ) == ( UBaseType_t ) 0 )
            {
                taskRESET_READY_PRIORITY( pxTCB->uxPriority );
            }

            vListInsertEnd( &xSuspendedTaskList, &( pxTCB->mut->xStateListItem ) );

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
            if( xSchedulerRunning != pdFALSE )
            {
                /* The current task has just been suspended. */
                portYIELD_WITHIN_API();
            }
            else
            {
                /* The scheduler is not running, but the task that was pointed
                 * to by pxCurrentTCB has just been suspended and pxCurrentTCB
                 * must be adjusted to point to a different task. */
                if( listCURRENT_LIST_LENGTH( &xSuspendedTaskList ) == uxCurrentNumberOfTasks ) /*lint !e931 Right has no side effect, just volatile. */
                {
                    /* No other tasks are ready, so set pxCurrentTCB back to
                     * NULL so when the next task is created pxCurrentTCB will
                     * be set to point to it no matter what its relative priority
                     * is. */
                    pxCurrentTCB = NULL;
                }
                else
                {
                    vTaskSwitchContext();
                }
            }
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

    traceTASK_SWITCHED_IN();

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

const char * pcTaskGetName( TaskHandle_t xTaskToQuery ) /*lint !e971 Unqualified char types are allowed for strings and single characters only. */
{
    TCB_t * pxTCB;

    /* If null is passed in here then the name of the calling task is being
     * queried. */
    pxTCB = prvGetTCBFromHandle( xTaskToQuery );
    configASSERT( pxTCB );
    return &( pxTCB->pcTaskName[ 0 ] );
}
/*-----------------------------------------------------------*/

BaseType_t xTaskIncrementTick( void )
{
    TCB_t * pxTCB;
    TickType_t xItemValue;
    BaseType_t xSwitchRequired = pdFALSE;

    /* Called by the portable layer each time a tick interrupt occurs.
     * Increments the tick then checks to see if the new tick value will cause any
     * tasks to be unblocked. */
    traceTASK_INCREMENT_TICK( xTickCount );

    /* Minor optimisation.  The tick count cannot change in this
     * block. */
    const TickType_t xConstTickCount = xTickCount + ( TickType_t ) 1;

    /* Increment the RTOS tick, switching the delayed and overflowed
     * delayed lists if it wraps to 0. */
    xTickCount = xConstTickCount;

    if( xConstTickCount == ( TickType_t ) 0U ) /*lint !e774 'if' does not always evaluate to false as it is looking for an overflow. */
    {
        taskSWITCH_DELAYED_LISTS();
    }

    /* See if this tick has made a timeout expire.  Tasks are stored in
     * the  queue in the order of their wake time - meaning once one task
     * has been found whose block time has not expired there is no need to
     * look any further down the list. */
    if( xConstTickCount >= xNextTaskUnblockTime )
    {
        for( ; ; )
        {
            if( listLIST_IS_EMPTY( pxDelayedTaskList ) != pdFALSE )
            {
                /* The delayed list is empty.  Set xNextTaskUnblockTime
                 * to the maximum possible value so it is extremely
                 * unlikely that the
                 * if( xTickCount >= xNextTaskUnblockTime ) test will pass
                 * next time through. */
                xNextTaskUnblockTime = portMAX_DELAY; /*lint !e961 MISRA exception as the casts are only redundant for some ports. */
                break;
            }
            else
            {
                /* The delayed list is not empty, get the value of the
                 * item at the head of the delayed list.  This is the time
                 * at which the task at the head of the delayed list must
                 * be removed from the Blocked state. */
                pxTCB = listGET_OWNER_OF_HEAD_ENTRY( pxDelayedTaskList ); /*lint !e9079 void * is used as this macro is used with timers and co-routines too.  Alignment is known to be fine as the type of the pointer stored and retrieved is the same. */
                xItemValue = listGET_LIST_ITEM_VALUE( &( pxTCB->mut->xStateListItem ) );

                if( xConstTickCount < xItemValue )
                {
                    /* It is not time to unblock this item yet, but the
                     * item value is the time at which the task at the head
                     * of the blocked list must be removed from the Blocked
                     * state -  so record the item value in
                     * xNextTaskUnblockTime. */
                    xNextTaskUnblockTime = xItemValue;
                    break; /*lint !e9011 Code structure here is deemed easier to understand with multiple breaks. */
                }

                /* It is time to remove the item from the Blocked state. */
                listREMOVE_ITEM( &( pxTCB->mut->xStateListItem ) );

                /* Place the unblocked task into the appropriate ready
                 * list. */
                prvAddTaskToReadyList( pxTCB );

                /* Preemption is on, but a context switch should
                 * only be performed if the unblocked task has a
                 * priority that is equal to or higher than the
                 * currently executing task. */
                if( pxTCB->uxPriority >= pxCurrentTCB->uxPriority )
                {
                    xSwitchRequired = pdTRUE;
                }
            }
        }
    }

    /* Tasks of equal priority to the currently running task will share
     * processing time (time slice) if preemption is on. */
    if( listCURRENT_LIST_LENGTH( &( pxReadyTasksLists[ pxCurrentTCB->uxPriority ] ) ) > ( UBaseType_t ) 1 )
    {
        xSwitchRequired = pdTRUE;
    }

    if( xYieldPending != pdFALSE )
    {
        xSwitchRequired = pdTRUE;
    }

    return xSwitchRequired;
}
/*-----------------------------------------------------------*/

void vTaskSwitchContext( void )
{
    xYieldPending = pdFALSE;
    traceTASK_SWITCHED_OUT();

    /* Check for stack overflow, if configured. */
    taskCHECK_FOR_STACK_OVERFLOW();

    /* Select a new task to run using either the generic C or port
     * optimised asm code. */
    taskSELECT_HIGHEST_PRIORITY_TASK(); /*lint !e9079 void * is used as this macro is used with timers and co-routines too.  Alignment is known to be fine as the type of the pointer stored and retrieved is the same. */
    traceTASK_SWITCHED_IN();
}
/*-----------------------------------------------------------*/

static void prvInitialiseTaskLists( void )
{
    UBaseType_t uxPriority;

    for( uxPriority = ( UBaseType_t ) 0U; uxPriority < ( UBaseType_t ) configMAX_PRIORITIES; uxPriority++ )
    {
        vListInitialise( &( pxReadyTasksLists[ uxPriority ] ) );
    }

    vListInitialise( &xDelayedTaskList1 );
    vListInitialise( &xDelayedTaskList2 );

    #if ( INCLUDE_vTaskSuspend == 1 )
        {
            vListInitialise( &xSuspendedTaskList );
        }
    #endif /* INCLUDE_vTaskSuspend */

    /* Start with pxDelayedTaskList using list1 and the pxOverflowDelayedTaskList
     * using list2. */
    pxDelayedTaskList = &xDelayedTaskList1;
    pxOverflowDelayedTaskList = &xDelayedTaskList2;
}
/*-----------------------------------------------------------*/

static void prvResetNextTaskUnblockTime( void )
{
    if( listLIST_IS_EMPTY( pxDelayedTaskList ) != pdFALSE )
    {
        /* The new current delayed list is empty.  Set xNextTaskUnblockTime to
         * the maximum possible value so it is  extremely unlikely that the
         * if( xTickCount >= xNextTaskUnblockTime ) test will pass until
         * there is an item in the delayed list. */
        xNextTaskUnblockTime = portMAX_DELAY;
    }
    else
    {
        /* The new current delayed list is not empty, get the value of
         * the item at the head of the delayed list.  This is the time at
         * which the task at the head of the delayed list should be removed
         * from the Blocked state. */
        xNextTaskUnblockTime = listGET_ITEM_VALUE_OF_HEAD_ENTRY( pxDelayedTaskList );
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

uint32_t ulTaskGenericNotifyTake( UBaseType_t uxIndexToWait,
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
                prvAddCurrentTaskToDelayedList( xTicksToWait, pdTRUE );
                traceTASK_NOTIFY_TAKE_BLOCK( uxIndexToWait );

                /* All ports are written to allow a yield in a critical
                 * section (some will yield immediately, others wait until the
                 * critical section exits) - but it is not something that
                 * application code should ever do. */
                portYIELD_WITHIN_API();
            }
        }
    }
    taskEXIT_CRITICAL();

    taskENTER_CRITICAL();
    {
        traceTASK_NOTIFY_TAKE( uxIndexToWait );
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

BaseType_t xTaskGenericNotifyWait( UBaseType_t uxIndexToWait,
                                   uint32_t ulBitsToClearOnEntry,
                                   uint32_t ulBitsToClearOnExit,
                                   uint32_t * pulNotificationValue,
                                   TickType_t xTicksToWait )
{
    BaseType_t xReturn;

    configASSERT( uxIndexToWait < configTASK_NOTIFICATION_ARRAY_ENTRIES );

    taskENTER_CRITICAL();
    {
        /* Only block if a notification is not already pending. */
        if( pxCurrentTCB->mut->ucNotifyState[ uxIndexToWait ] != taskNOTIFICATION_RECEIVED )
        {
            /* Clear bits in the task's notification value as bits may get
             * set  by the notifying task or interrupt.  This can be used to
             * clear the value to zero. */
            pxCurrentTCB->mut->ulNotifiedValue[ uxIndexToWait ] &= ~ulBitsToClearOnEntry;

            /* Mark this task as waiting for a notification. */
            pxCurrentTCB->mut->ucNotifyState[ uxIndexToWait ] = taskWAITING_NOTIFICATION;

            if( xTicksToWait > ( TickType_t ) 0 )
            {
                prvAddCurrentTaskToDelayedList( xTicksToWait, pdTRUE );
                traceTASK_NOTIFY_WAIT_BLOCK( uxIndexToWait );

                /* All ports are written to allow a yield in a critical
                 * section (some will yield immediately, others wait until the
                 * critical section exits) - but it is not something that
                 * application code should ever do. */
                portYIELD_WITHIN_API();
            }
        }
    }
    taskEXIT_CRITICAL();

    taskENTER_CRITICAL();
    {
        traceTASK_NOTIFY_WAIT( uxIndexToWait );

        if( pulNotificationValue != NULL )
        {
            /* Output the current notification value, which may or may not
             * have changed. */
            *pulNotificationValue = pxCurrentTCB->mut->ulNotifiedValue[ uxIndexToWait ];
        }

        /* If ucNotifyValue is set then either the task never entered the
         * blocked state (because a notification was already pending) or the
         * task unblocked because of a notification.  Otherwise the task
         * unblocked because of a timeout. */
        if( pxCurrentTCB->mut->ucNotifyState[ uxIndexToWait ] != taskNOTIFICATION_RECEIVED )
        {
            /* A notification was not received. */
            xReturn = pdFALSE;
        }
        else
        {
            /* A notification was already pending or a notification was
             * received while the task was waiting. */
            pxCurrentTCB->mut->ulNotifiedValue[ uxIndexToWait ] &= ~ulBitsToClearOnExit;
            xReturn = pdTRUE;
        }

        pxCurrentTCB->mut->ucNotifyState[ uxIndexToWait ] = taskNOT_WAITING_NOTIFICATION;
    }
    taskEXIT_CRITICAL();

    return xReturn;
}

/*-----------------------------------------------------------*/

BaseType_t xTaskGenericNotify( TaskHandle_t xTaskToNotify,
                               UBaseType_t uxIndexToNotify,
                               uint32_t ulValue,
                               eNotifyAction eAction,
                               uint32_t * pulPreviousNotificationValue )
{
    TCB_t * pxTCB;
    BaseType_t xReturn = pdPASS;
    uint8_t ucOriginalNotifyState;

    configASSERT( uxIndexToNotify < configTASK_NOTIFICATION_ARRAY_ENTRIES );
    configASSERT( xTaskToNotify );
    pxTCB = xTaskToNotify;

    taskENTER_CRITICAL();
    {
        if( pulPreviousNotificationValue != NULL )
        {
            *pulPreviousNotificationValue = pxTCB->mut->ulNotifiedValue[ uxIndexToNotify ];
        }

        ucOriginalNotifyState = pxTCB->mut->ucNotifyState[ uxIndexToNotify ];

        pxTCB->mut->ucNotifyState[ uxIndexToNotify ] = taskNOTIFICATION_RECEIVED;

        switch( eAction )
        {
            case eSetBits:
                pxTCB->mut->ulNotifiedValue[ uxIndexToNotify ] |= ulValue;
                break;

            case eIncrement:
                ( pxTCB->mut->ulNotifiedValue[ uxIndexToNotify ] )++;
                break;

            case eSetValueWithOverwrite:
                pxTCB->mut->ulNotifiedValue[ uxIndexToNotify ] = ulValue;
                break;

            case eSetValueWithoutOverwrite:

                if( ucOriginalNotifyState != taskNOTIFICATION_RECEIVED )
                {
                    pxTCB->mut->ulNotifiedValue[ uxIndexToNotify ] = ulValue;
                }
                else
                {
                    /* The value could not be written to the task. */
                    xReturn = pdFAIL;
                }

                break;

            case eNoAction:

                /* The task is being notified without its notify value being
                 * updated. */
                break;

            default:

                /* Should not get here if all enums are handled.
                 * Artificially force an assert by testing a value the
                 * compiler can't assume is const. */
                configASSERT( xTickCount == ( TickType_t ) 0 );

                break;
        }

        traceTASK_NOTIFY( uxIndexToNotify );

        /* If the task is in the blocked state specifically to wait for a
         * notification then unblock it now. */
        if( ucOriginalNotifyState == taskWAITING_NOTIFICATION )
        {
            listREMOVE_ITEM( &( pxTCB->mut->xStateListItem ) );
            prvAddTaskToReadyList( pxTCB );

            if( pxTCB->uxPriority > pxCurrentTCB->uxPriority )
            {
                /* The notified task has a priority above the currently
                 * executing task so a yield is required. */
                taskYIELD_IF_USING_PREEMPTION();
            }
        }
    }
    taskEXIT_CRITICAL();

    return xReturn;
}

/*-----------------------------------------------------------*/

BaseType_t xTaskGenericNotifyFromISR( TaskHandle_t xTaskToNotify,
                                      UBaseType_t uxIndexToNotify,
                                      uint32_t ulValue,
                                      eNotifyAction eAction,
                                      uint32_t * pulPreviousNotificationValue,
                                      BaseType_t * pxHigherPriorityTaskWoken )
{
    TCB_t * pxTCB;
    uint8_t ucOriginalNotifyState;
    BaseType_t xReturn = pdPASS;
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
        if( pulPreviousNotificationValue != NULL )
        {
            *pulPreviousNotificationValue = pxTCB->mut->ulNotifiedValue[ uxIndexToNotify ];
        }

        ucOriginalNotifyState = pxTCB->mut->ucNotifyState[ uxIndexToNotify ];
        pxTCB->mut->ucNotifyState[ uxIndexToNotify ] = taskNOTIFICATION_RECEIVED;

        switch( eAction )
        {
            case eSetBits:
                pxTCB->mut->ulNotifiedValue[ uxIndexToNotify ] |= ulValue;
                break;

            case eIncrement:
                ( pxTCB->mut->ulNotifiedValue[ uxIndexToNotify ] )++;
                break;

            case eSetValueWithOverwrite:
                pxTCB->mut->ulNotifiedValue[ uxIndexToNotify ] = ulValue;
                break;

            case eSetValueWithoutOverwrite:

                if( ucOriginalNotifyState != taskNOTIFICATION_RECEIVED )
                {
                    pxTCB->mut->ulNotifiedValue[ uxIndexToNotify ] = ulValue;
                }
                else
                {
                    /* The value could not be written to the task. */
                    xReturn = pdFAIL;
                }

                break;

            case eNoAction:

                /* The task is being notified without its notify value being
                 * updated. */
                break;

            default:

                /* Should not get here if all enums are handled.
                 * Artificially force an assert by testing a value the
                 * compiler can't assume is const. */
                configASSERT( xTickCount == ( TickType_t ) 0 );
                break;
        }

        traceTASK_NOTIFY_FROM_ISR( uxIndexToNotify );

        /* If the task is in the blocked state specifically to wait for a
         * notification then unblock it now. */
        if( ucOriginalNotifyState == taskWAITING_NOTIFICATION )
        {
            listREMOVE_ITEM( &( pxTCB->mut->xStateListItem ) );
            prvAddTaskToReadyList( pxTCB );

            if( pxTCB->uxPriority > pxCurrentTCB->uxPriority )
            {
                /* The notified task has a priority above the currently
                 * executing task so a yield is required. */
                if( pxHigherPriorityTaskWoken != NULL )
                {
                    *pxHigherPriorityTaskWoken = pdTRUE;
                }

                /* Mark that a yield is pending in case the user is not
                 * using the "xHigherPriorityTaskWoken" parameter to an ISR
                 * safe FreeRTOS function. */
                xYieldPending = pdTRUE;
            }
        }
    }
    portCLEAR_INTERRUPT_MASK_FROM_ISR( uxSavedInterruptStatus );

    return xReturn;
}

/*-----------------------------------------------------------*/

void vTaskGenericNotifyGiveFromISR( TaskHandle_t xTaskToNotify,
                                    UBaseType_t uxIndexToNotify,
                                    BaseType_t * pxHigherPriorityTaskWoken )
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

        traceTASK_NOTIFY_GIVE_FROM_ISR( uxIndexToNotify );

        /* If the task is in the blocked state specifically to wait for a
         * notification then unblock it now. */
        if( ucOriginalNotifyState == taskWAITING_NOTIFICATION )
        {
            listREMOVE_ITEM( &( pxTCB->mut->xStateListItem ) );
            prvAddTaskToReadyList( pxTCB );

            if( pxTCB->uxPriority > pxCurrentTCB->uxPriority )
            {
                /* The notified task has a priority above the currently
                 * executing task so a yield is required. */
                if( pxHigherPriorityTaskWoken != NULL )
                {
                    *pxHigherPriorityTaskWoken = pdTRUE;
                }

                /* Mark that a yield is pending in case the user is not
                 * using the "xHigherPriorityTaskWoken" parameter in an ISR
                 * safe FreeRTOS function. */
                xYieldPending = pdTRUE;
            }
        }
    }
    portCLEAR_INTERRUPT_MASK_FROM_ISR( uxSavedInterruptStatus );
}

/*-----------------------------------------------------------*/

BaseType_t xTaskGenericNotifyStateClear( TaskHandle_t xTask,
                                         UBaseType_t uxIndexToClear )
{
    TCB_t * pxTCB;
    BaseType_t xReturn;

    configASSERT( uxIndexToClear < configTASK_NOTIFICATION_ARRAY_ENTRIES );

    /* If null is passed in here then it is the calling task that is having
     * its notification state cleared. */
    pxTCB = prvGetTCBFromHandle( xTask );

    taskENTER_CRITICAL();
    {
        if( pxTCB->mut->ucNotifyState[ uxIndexToClear ] == taskNOTIFICATION_RECEIVED )
        {
            pxTCB->mut->ucNotifyState[ uxIndexToClear ] = taskNOT_WAITING_NOTIFICATION;
            xReturn = pdPASS;
        }
        else
        {
            xReturn = pdFAIL;
        }
    }
    taskEXIT_CRITICAL();

    return xReturn;
}

/*-----------------------------------------------------------*/

uint32_t ulTaskGenericNotifyValueClear( TaskHandle_t xTask,
                                        UBaseType_t uxIndexToClear,
                                        uint32_t ulBitsToClear )
{
    TCB_t * pxTCB;
    uint32_t ulReturn;

    /* If null is passed in here then it is the calling task that is having
     * its notification state cleared. */
    pxTCB = prvGetTCBFromHandle( xTask );

    taskENTER_CRITICAL();
    {
        /* Return the notification as it was before the bits were cleared,
         * then clear the bit mask. */
        ulReturn = pxTCB->mut->ulNotifiedValue[ uxIndexToClear ];
        pxTCB->mut->ulNotifiedValue[ uxIndexToClear ] &= ~ulBitsToClear;
    }
    taskEXIT_CRITICAL();

    return ulReturn;
}

/*-----------------------------------------------------------*/

static void prvAddCurrentTaskToDelayedList( TickType_t xTicksToWait,
                                            const BaseType_t xCanBlockIndefinitely )
{
    TickType_t xTimeToWake;
    const TickType_t xConstTickCount = xTickCount;

    /* Remove the task from the ready list before adding it to the blocked list
     * as the same list item is used for both lists. */
    if( uxListRemove( &( pxCurrentTCB->mut->xStateListItem ) ) == ( UBaseType_t ) 0 )
    {
        /* The current task must be in a ready list, so there is no need to
         * check, and the port reset macro can be called directly. */
        portRESET_READY_PRIORITY( pxCurrentTCB->uxPriority, uxTopReadyPriority ); /*lint !e931 pxCurrentTCB cannot change as it is the calling task.  pxCurrentTCB->uxPriority and uxTopReadyPriority cannot change as called with scheduler suspended or in a critical section. */
    }

    #if ( INCLUDE_vTaskSuspend == 1 )
        {
            if( ( xTicksToWait == portMAX_DELAY ) && ( xCanBlockIndefinitely != pdFALSE ) )
            {
                /* Add the task to the suspended task list instead of a delayed task
                 * list to ensure it is not woken by a timing event.  It will block
                 * indefinitely. */
                listINSERT_END( &xSuspendedTaskList, &( pxCurrentTCB->mut->xStateListItem ) );
            }
            else
            {
                /* Calculate the time at which the task should be woken if the event
                 * does not occur.  This may overflow but this doesn't matter, the
                 * kernel will manage it correctly. */
                xTimeToWake = xConstTickCount + xTicksToWait;

                /* The list item will be inserted in wake time order. */
                listSET_LIST_ITEM_VALUE( &( pxCurrentTCB->mut->xStateListItem ), xTimeToWake );

                if( xTimeToWake < xConstTickCount )
                {
                    /* Wake time has overflowed.  Place this item in the overflow
                     * list. */
                    vListInsert( pxOverflowDelayedTaskList, &( pxCurrentTCB->mut->xStateListItem ) );
                }
                else
                {
                    /* The wake time has not overflowed, so the current block list
                     * is used. */
                    vListInsert( pxDelayedTaskList, &( pxCurrentTCB->mut->xStateListItem ) );

                    /* If the task entering the blocked state was placed at the
                     * head of the list of blocked tasks then xNextTaskUnblockTime
                     * needs to be updated too. */
                    if( xTimeToWake < xNextTaskUnblockTime )
                    {
                        xNextTaskUnblockTime = xTimeToWake;
                    }
                }
            }
        }
    #else /* INCLUDE_vTaskSuspend */
        {
            /* Calculate the time at which the task should be woken if the event
             * does not occur.  This may overflow but this doesn't matter, the kernel
             * will manage it correctly. */
            xTimeToWake = xConstTickCount + xTicksToWait;

            /* The list item will be inserted in wake time order. */
            listSET_LIST_ITEM_VALUE( &( pxCurrentTCB->mut->xStateListItem ), xTimeToWake );

            if( xTimeToWake < xConstTickCount )
            {
                /* Wake time has overflowed.  Place this item in the overflow list. */
                vListInsert( pxOverflowDelayedTaskList, &( pxCurrentTCB->mut->xStateListItem ) );
            }
            else
            {
                /* The wake time has not overflowed, so the current block list is used. */
                vListInsert( pxDelayedTaskList, &( pxCurrentTCB->mut->xStateListItem ) );

                /* If the task entering the blocked state was placed at the head of the
                 * list of blocked tasks then xNextTaskUnblockTime needs to be updated
                 * too. */
                if( xTimeToWake < xNextTaskUnblockTime )
                {
                    xNextTaskUnblockTime = xTimeToWake;
                }
            }

            /* Avoid compiler warning when INCLUDE_vTaskSuspend is not 1. */
            ( void ) xCanBlockIndefinitely;
        }
    #endif /* INCLUDE_vTaskSuspend */
}
