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

#define RTOS_STACK_SIZE ( 1000 )

typedef enum {
    NOT_RESTARTABLE = 0,
    RESTARTABLE,
    RESTART_ON_RESCHEDULE,
} restartable_t;

typedef struct {
    uint64_t iteration_1;
    uint64_t iteration_2;
    uint8_t max_attempts;
} scrubber_pend_t;

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
    uint32_t recursive_exception;        /*< MUST BE THE SECOND MEMBER OF THE TCB STRUCT */
    bool needs_start;
    bool hit_restart;

    // only used for clips
    bool            clip_running;
    uint32_t        clip_next_tick;
    scrubber_pend_t clip_pend;
    uint64_t        clip_max_nanos;

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

// array containing all tasktable entries produced in TASK_REGISTER
// (this array is generated from fragments by the linker)
extern const TCB_t tasktable_start[];
extern const TCB_t tasktable_end[];

typedef struct
{
    const TCB_t *task;
    uint32_t nanos;
} schedule_entry_t;

// array containing the scheduling order for these tasks, defined statically using TASK_SCHEDULING_ORDER
extern const schedule_entry_t task_scheduling_order[];
extern const uint32_t         task_scheduling_order_length;

/*
 * Type by which tasks are referenced.  For example, a call to xTaskCreate
 * returns (via a pointer parameter) an TaskHandle_t variable that can then
 * be used as a parameter to vTaskDelete to delete the task.
 *
 * \defgroup TaskHandle_t TaskHandle_t
 * \ingroup Tasks
 */
typedef TCB_t * TaskHandle_t;

extern TCB_t * volatile pxCurrentTCB;

extern uint64_t schedule_loads;
extern uint32_t schedule_ticks;
extern local_time_t schedule_period_start;
extern local_time_t schedule_last;
extern local_time_t schedule_epoch_start;

/*
 * Resume a task, restoring registers from the specified stack pointer.
 */
extern void resume_restore_context(volatile void *stack) __attribute__((noreturn));
extern void start_clip_context(volatile void *stack) __attribute__((noreturn));

void vTaskStartScheduler( void );
void vTaskSwitchContext( void );

#endif /* INC_TASK_H */
