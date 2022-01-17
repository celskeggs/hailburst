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


#ifndef INC_TASK_H
#define INC_TASK_H

#ifndef INC_FREERTOS_H
    #error "include FreeRTOS.h must appear in source files before include task.h"
#endif

/*-----------------------------------------------------------
* MACROS AND DEFINITIONS
*----------------------------------------------------------*/

/* The direct to task notification feature used to have only a single notification
 * per task.  Now there is an array of notifications per task that is dimensioned by
 * configTASK_NOTIFICATION_ARRAY_ENTRIES.  For backward compatibility, any use of the
 * original direct to task notification defaults to using the first index in the
 * array. */
#define tskDEFAULT_INDEX_TO_NOTIFY     ( 0 )

/*
 * Defines the prototype to which the application task hook function must
 * conform.
 */
typedef BaseType_t (* TaskHookFunction_t)( void * );

#define RTOS_STACK_SIZE ( 1000 )

typedef enum {
    NOT_RESTARTABLE = 0,
    RESTARTABLE,
} restartable_t;

typedef enum {
    TS_DELETED          = 0,
    TS_READY            = 1,
    TS_DELAYED          = 2,
    TS_DELAYED_OVERFLOW = 3,
    TS_SUSPENDED        = 4,
} task_state_t;

/**
 * task. h
 *
 * Task control block.  A task control block (TCB) is allocated for each task,
 * and stores task state information, including a pointer to the task's context
 * (the task's run time environment, including register values)
 */
typedef struct
{
    volatile StackType_t * pxTopOfStack; /*< Points to the location of the last item placed on the tasks stack.  THIS MUST BE THE FIRST MEMBER OF THE TCB STRUCT. */
    bool needs_restart;
    bool hit_restart;
    task_state_t task_state;
    TickType_t delay_deadline;

    volatile uint32_t ulNotifiedValue[ configTASK_NOTIFICATION_ARRAY_ENTRIES ];
    volatile uint8_t ucNotifyState[ configTASK_NOTIFICATION_ARRAY_ENTRIES ];
} TCB_mut_t;

// this is an immutable structure
typedef struct TCB_st
{
    TCB_mut_t * const mut;                      /*< THIS MUST BE THE FIRST MEMBER OF THE TCB STRUCT. */

    void (* const start_routine)(void*);
    void * const start_arg;
    const restartable_t restartable;
    StackType_t * const pxStack;                /*< Points to the start of the stack of size RTOS_STACK_SIZE. */
    const char * const pcTaskName;              /*< Descriptive name given to the task when created.  Facilitates debugging only. */
} const TCB_t;

// array containing all .tasktable entries produced in TASK_REGISTER
// (this array is generated from fragments by the linker)
extern TCB_t tasktable_start[];
extern TCB_t tasktable_end[];

/*
 * Type by which tasks are referenced.  For example, a call to xTaskCreate
 * returns (via a pointer parameter) an TaskHandle_t variable that can then
 * be used as a parameter to vTaskDelete to delete the task.
 *
 * \defgroup TaskHandle_t TaskHandle_t
 * \ingroup Tasks
 */
typedef TCB_t * TaskHandle_t;

/* Actions that can be performed when vTaskNotify() is called. */
typedef enum
{
    eNoAction = 0,            /* Notify the task without updating its notify value. */
    eSetBits,                 /* Set bits in the task's notification value. */
    eIncrement,               /* Increment the task's notification value. */
    eSetValueWithOverwrite,   /* Set the task's notification value to a specific value even if the previous value has not yet been read by the task. */
    eSetValueWithoutOverwrite /* Set the task's notification value if the previous value has been read by the task. */
} eNotifyAction;

/**
 * task. h
 *
 * Macro for forcing a context switch.
 *
 * \defgroup taskYIELD taskYIELD
 * \ingroup SchedulerControl
 */
#define taskYIELD()                        portYIELD()

/**
 * task. h
 *
 * Macro to mark the start of a critical code region.  Preemptive context
 * switches cannot occur when in a critical region.
 *
 * NOTE: This may alter the stack (depending on the portable implementation)
 * so must be used with care!
 *
 * \defgroup taskENTER_CRITICAL taskENTER_CRITICAL
 * \ingroup SchedulerControl
 */
#define taskENTER_CRITICAL()               portENTER_CRITICAL()
#define taskENTER_CRITICAL_FROM_ISR()      portSET_INTERRUPT_MASK_FROM_ISR()

/**
 * task. h
 *
 * Macro to mark the end of a critical code region.  Preemptive context
 * switches cannot occur when in a critical region.
 *
 * NOTE: This may alter the stack (depending on the portable implementation)
 * so must be used with care!
 *
 * \defgroup taskEXIT_CRITICAL taskEXIT_CRITICAL
 * \ingroup SchedulerControl
 */
#define taskEXIT_CRITICAL()                portEXIT_CRITICAL()
#define taskEXIT_CRITICAL_FROM_ISR( x )    portCLEAR_INTERRUPT_MASK_FROM_ISR( x )

/**
 * task. h
 *
 * Macro to disable all maskable interrupts.
 *
 * \defgroup taskDISABLE_INTERRUPTS taskDISABLE_INTERRUPTS
 * \ingroup SchedulerControl
 */
#define taskDISABLE_INTERRUPTS()           portDISABLE_INTERRUPTS()

/**
 * task. h
 *
 * Macro to enable microcontroller interrupts.
 *
 * \defgroup taskENABLE_INTERRUPTS taskENABLE_INTERRUPTS
 * \ingroup SchedulerControl
 */
#define taskENABLE_INTERRUPTS()            portENABLE_INTERRUPTS()

/* Definitions returned by xTaskGetSchedulerState(). */
#define taskSCHEDULER_NOT_STARTED    ( ( BaseType_t ) 1 )
#define taskSCHEDULER_RUNNING        ( ( BaseType_t ) 2 )


/*-----------------------------------------------------------
* TASK CREATION API
*----------------------------------------------------------*/

/**
 * task. h
 * @code{c}
 * void vTaskDelete( TaskHandle_t xTaskToDelete );
 * @endcode
 *
 * INCLUDE_vTaskDelete must be defined as 1 for this function to be available.
 * See the configuration section for more information.
 *
 * Remove a task from the RTOS real time kernel's management.  The task being
 * deleted will be removed from all ready, blocked, suspended and event lists.
 *
 * NOTE:  The idle task is responsible for freeing the kernel allocated
 * memory from tasks that have been deleted.  It is therefore important that
 * the idle task is not starved of microcontroller processing time if your
 * application makes any calls to vTaskDelete ().  Memory allocated by the
 * task code is not automatically freed, and should be freed before the task
 * is deleted.
 *
 * See the demo application file death.c for sample code that utilises
 * vTaskDelete ().
 *
 * @param xTaskToDelete The handle of the task to be deleted.  Passing NULL will
 * cause the calling task to be deleted.
 *
 * Example usage:
 * @code{c}
 * void vOtherFunction( void )
 * {
 * TaskHandle_t xHandle;
 *
 *   // Create the task, storing the handle.
 *   xTaskCreate( vTaskCode, "NAME", STACK_SIZE, NULL, &xHandle );
 *
 *   // Use the handle to delete the task.
 *   vTaskDelete( xHandle );
 * }
 * @endcode
 * \defgroup vTaskDelete vTaskDelete
 * \ingroup Tasks
 */
void vTaskDelete( TaskHandle_t xTaskToDelete );

/*-----------------------------------------------------------
* TASK CONTROL API
*----------------------------------------------------------*/

/**
 * task. h
 * @code{c}
 * void vTaskDelay( const TickType_t xTicksToDelay );
 * @endcode
 *
 * Delay a task for a given number of ticks.  The actual time that the
 * task remains blocked depends on the tick rate.  The constant
 * portTICK_PERIOD_MS can be used to calculate real time from the tick
 * rate - with the resolution of one tick period.
 *
 * INCLUDE_vTaskDelay must be defined as 1 for this function to be available.
 * See the configuration section for more information.
 *
 *
 * vTaskDelay() specifies a time at which the task wishes to unblock relative to
 * the time at which vTaskDelay() is called.  For example, specifying a block
 * period of 100 ticks will cause the task to unblock 100 ticks after
 * vTaskDelay() is called.  vTaskDelay() does not therefore provide a good method
 * of controlling the frequency of a periodic task as the path taken through the
 * code, as well as other task and interrupt activity, will affect the frequency
 * at which vTaskDelay() gets called and therefore the time at which the task
 * next executes.  See xTaskDelayUntil() for an alternative API function designed
 * to facilitate fixed frequency execution.  It does this by specifying an
 * absolute time (rather than a relative time) at which the calling task should
 * unblock.
 *
 * @param xTicksToDelay The amount of time, in tick periods, that
 * the calling task should block.
 *
 * Example usage:
 *
 * void vTaskFunction( void * pvParameters )
 * {
 * // Block for 500ms.
 * const TickType_t xDelay = 500 / portTICK_PERIOD_MS;
 *
 *   for( ;; )
 *   {
 *       // Simply toggle the LED every 500ms, blocking between each toggle.
 *       vToggleLED();
 *       vTaskDelay( xDelay );
 *   }
 * }
 *
 * \defgroup vTaskDelay vTaskDelay
 * \ingroup TaskCtrl
 */
void vTaskDelay( const TickType_t xTicksToDelay );

/**
 * task. h
 * @code{c}
 * BaseType_t xTaskDelayUntil( TickType_t *pxPreviousWakeTime, const TickType_t xTimeIncrement );
 * @endcode
 *
 * INCLUDE_xTaskDelayUntil must be defined as 1 for this function to be available.
 * See the configuration section for more information.
 *
 * Delay a task until a specified time.  This function can be used by periodic
 * tasks to ensure a constant execution frequency.
 *
 * This function differs from vTaskDelay () in one important aspect:  vTaskDelay () will
 * cause a task to block for the specified number of ticks from the time vTaskDelay () is
 * called.  It is therefore difficult to use vTaskDelay () by itself to generate a fixed
 * execution frequency as the time between a task starting to execute and that task
 * calling vTaskDelay () may not be fixed [the task may take a different path though the
 * code between calls, or may get interrupted or preempted a different number of times
 * each time it executes].
 *
 * Whereas vTaskDelay () specifies a wake time relative to the time at which the function
 * is called, xTaskDelayUntil () specifies the absolute (exact) time at which it wishes to
 * unblock.
 *
 * The macro pdMS_TO_TICKS() can be used to calculate the number of ticks from a
 * time specified in milliseconds with a resolution of one tick period.
 *
 * @param pxPreviousWakeTime Pointer to a variable that holds the time at which the
 * task was last unblocked.  The variable must be initialised with the current time
 * prior to its first use (see the example below).  Following this the variable is
 * automatically updated within xTaskDelayUntil ().
 *
 * @param xTimeIncrement The cycle time period.  The task will be unblocked at
 * time *pxPreviousWakeTime + xTimeIncrement.  Calling xTaskDelayUntil with the
 * same xTimeIncrement parameter value will cause the task to execute with
 * a fixed interface period.
 *
 * @return Value which can be used to check whether the task was actually delayed.
 * Will be pdTRUE if the task way delayed and pdFALSE otherwise.  A task will not
 * be delayed if the next expected wake time is in the past.
 *
 * Example usage:
 * @code{c}
 * // Perform an action every 10 ticks.
 * void vTaskFunction( void * pvParameters )
 * {
 * TickType_t xLastWakeTime;
 * const TickType_t xFrequency = 10;
 * BaseType_t xWasDelayed;
 *
 *     // Initialise the xLastWakeTime variable with the current time.
 *     xLastWakeTime = xTaskGetTickCount ();
 *     for( ;; )
 *     {
 *         // Wait for the next cycle.
 *         xWasDelayed = xTaskDelayUntil( &xLastWakeTime, xFrequency );
 *
 *         // Perform action here. xWasDelayed value can be used to determine
 *         // whether a deadline was missed if the code here took too long.
 *     }
 * }
 * @endcode
 * \defgroup xTaskDelayUntil xTaskDelayUntil
 * \ingroup TaskCtrl
 */
BaseType_t xTaskDelayUntil( TickType_t * const pxPreviousWakeTime,
                            const TickType_t xTimeIncrement );

/**
 * task. h
 * @code{c}
 * void vTaskSuspend( TaskHandle_t xTaskToSuspend );
 * @endcode
 *
 * INCLUDE_vTaskSuspend must be defined as 1 for this function to be available.
 * See the configuration section for more information.
 *
 * Suspend any task.  When suspended a task will never get any microcontroller
 * processing time.
 *
 * Calls to vTaskSuspend are not accumulative -
 * i.e. calling vTaskSuspend () twice on the same task still only requires one
 * call to vTaskResume () to ready the suspended task.
 *
 * @param xTaskToSuspend Handle to the task being suspended.  Passing a NULL
 * handle will cause the calling task to be suspended.
 *
 * Example usage:
 * @code{c}
 * void vAFunction( void )
 * {
 * TaskHandle_t xHandle;
 *
 *   // Create a task, storing the handle.
 *   xTaskCreate( vTaskCode, "NAME", STACK_SIZE, NULL, &xHandle );
 *
 *   // ...
 *
 *   // Use the handle to suspend the created task.
 *   vTaskSuspend( xHandle );
 *
 *   // ...
 *
 *   // The created task will not run during this period, unless
 *   // another task calls vTaskResume( xHandle ).
 *
 *   //...
 *
 *
 *   // Suspend ourselves.
 *   vTaskSuspend( NULL );
 *
 *   // We cannot get here unless another task calls vTaskResume
 *   // with our handle as the parameter.
 * }
 * @endcode
 * \defgroup vTaskSuspend vTaskSuspend
 * \ingroup TaskCtrl
 */
void vTaskSuspend( TaskHandle_t xTaskToSuspend );

/*-----------------------------------------------------------
* SCHEDULER CONTROL
*----------------------------------------------------------*/

/**
 * task. h
 * @code{c}
 * void vTaskStartScheduler( void );
 * @endcode
 *
 * Starts the real time kernel tick processing.  After calling the kernel
 * has control over which tasks are executed and when.
 *
 * See the demo application file main.c for an example of creating
 * tasks and starting the kernel.
 *
 * Example usage:
 * @code{c}
 * void vAFunction( void )
 * {
 *   // Create at least one task before starting the kernel.
 *   xTaskCreate( vTaskCode, "NAME", STACK_SIZE, NULL, NULL );
 *
 *   // Start the real time kernel with preemption.
 *   vTaskStartScheduler ();
 *
 *   // Will not get here unless a task calls vTaskEndScheduler ()
 * }
 * @endcode
 *
 * \defgroup vTaskStartScheduler vTaskStartScheduler
 * \ingroup SchedulerControl
 */
void vTaskStartScheduler( void );

/*-----------------------------------------------------------
* TASK UTILITIES
*----------------------------------------------------------*/

/**
 * task. h
 * @code{c}
 * TickType_t xTaskGetTickCount( void );
 * @endcode
 *
 * @return The count of ticks since vTaskStartScheduler was called.
 *
 * \defgroup xTaskGetTickCount xTaskGetTickCount
 * \ingroup TaskUtils
 */
TickType_t xTaskGetTickCount( void );

#if ( configCHECK_FOR_STACK_OVERFLOW > 0 )

/**
 * task.h
 * @code{c}
 * void vApplicationStackOverflowHook( TaskHandle_t xTask, const char *pcTaskName);
 * @endcode
 *
 * The application stack overflow hook is called when a stack overflow is detected for a task.
 *
 * Details on stack overflow detection can be found here: https://www.FreeRTOS.org/Stacks-and-stack-overflow-checking.html
 *
 * @param xTask the task that just exceeded its stack boundaries.
 * @param pcTaskName A character string containing the name of the offending task.
 */
    void vApplicationStackOverflowHook( TaskHandle_t xTask,
                                        const char * pcTaskName );

#endif

BaseType_t xTaskNotifyGiveIndexed( TaskHandle_t xTaskToNotify,
                                   UBaseType_t uxIndexToNotify );

void vTaskNotifyGiveIndexedFromISR( TaskHandle_t xTaskToNotify,
                                    UBaseType_t uxIndexToNotify );

uint32_t ulTaskNotifyTakeIndexed( UBaseType_t uxIndexToWaitOn,
                                  BaseType_t xClearCountOnExit,
                                  TickType_t xTicksToWait );

/*-----------------------------------------------------------
* SCHEDULER INTERNALS AVAILABLE FOR PORTING PURPOSES
*----------------------------------------------------------*/

/*
 * THIS FUNCTION MUST NOT BE USED FROM APPLICATION CODE.  IT IS ONLY
 * INTENDED FOR USE WHEN IMPLEMENTING A PORT OF THE SCHEDULER AND IS
 * AN INTERFACE WHICH IS FOR THE EXCLUSIVE USE OF THE SCHEDULER.
 *
 * Called from the real time kernel tick (either preemptive or cooperative),
 * this increments the tick count and checks if any tasks that are blocked
 * for a finite period required removing from a blocked list and placing on
 * a ready list.  If a non-zero value is returned then a context switch is
 * required because either:
 *   + A task was removed from a blocked list because its timeout had expired,
 *     or
 *   + Time slicing is in use and there is another task ready to run.
 */
BaseType_t xTaskIncrementTick( void );

/*
 * THIS FUNCTION MUST NOT BE USED FROM APPLICATION CODE.  IT IS ONLY
 * INTENDED FOR USE WHEN IMPLEMENTING A PORT OF THE SCHEDULER AND IS
 * AN INTERFACE WHICH IS FOR THE EXCLUSIVE USE OF THE SCHEDULER.
 *
 * Sets the pointer to the current TCB to the TCB of the next task
 * that is ready to run.
 */
void vTaskSwitchContext( void );

/*
 * Return the handle of the calling task.
 */
TaskHandle_t xTaskGetCurrentTaskHandle( void );

/*
 * Returns the scheduler state as taskSCHEDULER_RUNNING, or taskSCHEDULER_NOT_STARTED.
 */
BaseType_t xTaskGetSchedulerState( void );

#endif /* INC_TASK_H */
