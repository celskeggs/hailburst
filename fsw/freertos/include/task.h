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

    // these are just 0 or 1, but in a full uint32_t to help with atomicity
    uint32_t roused_task;
    uint32_t roused_local;
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

void thread_start_internal( TCB_t * pxNewTCB );
void thread_restart_other_task( TCB_t * pxTCB );

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
* SCHEDULER INTERNALS AVAILABLE FOR PORTING PURPOSES
*----------------------------------------------------------*/

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
