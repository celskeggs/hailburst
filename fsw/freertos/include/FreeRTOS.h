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

#ifndef INC_FREERTOS_H
#define INC_FREERTOS_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h> /* READ COMMENT ABOVE. */

/* Application specific configuration options. */
#include "FreeRTOSConfig.h"

/* Basic FreeRTOS definitions. */
#include "projdefs.h"

/* Definitions specific to the port being used. */
#include "portable.h"

/*
 * Check all the required application specific macros have been defined.
 * These macros are application specific and (as downloaded) are defined
 * within FreeRTOSConfig.h.
 */

#ifndef configMINIMAL_STACK_SIZE
    #error Missing definition:  configMINIMAL_STACK_SIZE must be defined in FreeRTOSConfig.h.  configMINIMAL_STACK_SIZE defines the size (in words) of the stack allocated to the idle task.  Refer to the demo project provided for your port for a suitable value.
#endif

#ifndef INCLUDE_vTaskDelete
    #define INCLUDE_vTaskDelete    0
#endif

#ifndef INCLUDE_vTaskSuspend
    #define INCLUDE_vTaskSuspend    0
#endif

#ifndef INCLUDE_xTaskDelayUntil
    #define INCLUDE_xTaskDelayUntil    0
#endif

#ifndef INCLUDE_vTaskDelay
    #define INCLUDE_vTaskDelay    0
#endif

#ifndef INCLUDE_xTaskGetSchedulerState
    #define INCLUDE_xTaskGetSchedulerState    0
#endif

#ifndef INCLUDE_xTaskGetCurrentTaskHandle
    #define INCLUDE_xTaskGetCurrentTaskHandle    0
#endif

#ifndef configASSERT
    #define configASSERT( x )
    #define configASSERT_DEFINED    0
#else
    #define configASSERT_DEFINED    1
#endif

#ifndef portMEMORY_BARRIER
    #define portMEMORY_BARRIER()
#endif

#ifndef portSOFTWARE_BARRIER
    #define portSOFTWARE_BARRIER()
#endif

#ifndef portSET_INTERRUPT_MASK_FROM_ISR
    #define portSET_INTERRUPT_MASK_FROM_ISR()    0
#endif

#ifndef portCLEAR_INTERRUPT_MASK_FROM_ISR
    #define portCLEAR_INTERRUPT_MASK_FROM_ISR( uxSavedStatusValue )    ( void ) uxSavedStatusValue
#endif

#ifndef portPOINTER_SIZE_TYPE
    #define portPOINTER_SIZE_TYPE    uint32_t
#endif

#ifndef traceTASK_SWITCHED_IN

/* Called after a task has been selected to run.  pxCurrentTCB holds a pointer
 * to the task control block of the selected task. */
    #define traceTASK_SWITCHED_IN()
#endif

#ifndef traceTASK_SWITCHED_OUT

/* Called before a task has been selected to run.  pxCurrentTCB holds a pointer
 * to the task control block of the task being switched out. */
    #define traceTASK_SWITCHED_OUT()
#endif

#ifndef configCHECK_FOR_STACK_OVERFLOW
    #define configCHECK_FOR_STACK_OVERFLOW    0
#endif

/* The following event macros are embedded in the kernel API calls. */

#ifndef traceMOVED_TASK_TO_READY_STATE
    #define traceMOVED_TASK_TO_READY_STATE( pxTCB )
#endif

#ifndef tracePOST_MOVED_TASK_TO_READY_STATE
    #define tracePOST_MOVED_TASK_TO_READY_STATE( pxTCB )
#endif

#ifndef traceTASK_DELAY_UNTIL
    #define traceTASK_DELAY_UNTIL( x )
#endif

#ifndef traceTASK_DELAY
    #define traceTASK_DELAY()
#endif

#ifndef traceTASK_SUSPEND
    #define traceTASK_SUSPEND( pxTaskToSuspend )
#endif

#ifndef traceTASK_INCREMENT_TICK
    #define traceTASK_INCREMENT_TICK( xTickCount )
#endif

#ifndef traceTASK_NOTIFY_TAKE_BLOCK
    #define traceTASK_NOTIFY_TAKE_BLOCK( uxIndexToWait )
#endif

#ifndef traceTASK_NOTIFY_TAKE
    #define traceTASK_NOTIFY_TAKE( uxIndexToWait )
#endif

#ifndef traceTASK_NOTIFY_WAIT_BLOCK
    #define traceTASK_NOTIFY_WAIT_BLOCK( uxIndexToWait )
#endif

#ifndef traceTASK_NOTIFY_WAIT
    #define traceTASK_NOTIFY_WAIT( uxIndexToWait )
#endif

#ifndef traceTASK_NOTIFY
    #define traceTASK_NOTIFY( uxIndexToNotify )
#endif

#ifndef traceTASK_NOTIFY_FROM_ISR
    #define traceTASK_NOTIFY_FROM_ISR( uxIndexToNotify )
#endif

#ifndef traceTASK_NOTIFY_GIVE_FROM_ISR
    #define traceTASK_NOTIFY_GIVE_FROM_ISR( uxIndexToNotify )
#endif

#ifndef portYIELD_WITHIN_API
    #define portYIELD_WITHIN_API    portYIELD
#endif

#ifndef portASSERT_IF_INTERRUPT_PRIORITY_INVALID
    #define portASSERT_IF_INTERRUPT_PRIORITY_INVALID()
#endif

#ifndef configTASK_NOTIFICATION_ARRAY_ENTRIES
    #define configTASK_NOTIFICATION_ARRAY_ENTRIES    1
#endif

#if configTASK_NOTIFICATION_ARRAY_ENTRIES < 1
    #error configTASK_NOTIFICATION_ARRAY_ENTRIES must be at least 1
#endif

#ifndef configSTACK_DEPTH_TYPE

/* Defaults to uint16_t for backward compatibility, but can be overridden
 * in FreeRTOSConfig.h if uint16_t is too restrictive. */
    #define configSTACK_DEPTH_TYPE    uint16_t
#endif

#ifndef configINITIAL_TICK_COUNT
    #define configINITIAL_TICK_COUNT    0
#endif

/* The tick type can be read atomically, so critical sections used when the
 * tick count is returned can be defined away. */
#define portTICK_TYPE_ENTER_CRITICAL()
#define portTICK_TYPE_EXIT_CRITICAL()
#define portTICK_TYPE_SET_INTERRUPT_MASK_FROM_ISR()         0
#define portTICK_TYPE_CLEAR_INTERRUPT_MASK_FROM_ISR( x )    ( void ) x

/* Set configUSE_TASK_FPU_SUPPORT to 0 to omit floating point support even
 * if floating point hardware is otherwise supported by the FreeRTOS port in use.
 * This constant is not supported by all FreeRTOS ports that include floating
 * point support. */
#ifndef configUSE_TASK_FPU_SUPPORT
    #define configUSE_TASK_FPU_SUPPORT    1
#endif

#endif /* INC_FREERTOS_H */
