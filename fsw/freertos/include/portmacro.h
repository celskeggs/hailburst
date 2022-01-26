/*
 * This file is borrowed from the FreeRTOS GCC/ARM_CA9 port.
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

#ifndef PORTMACRO_H
#define PORTMACRO_H

/*-----------------------------------------------------------
 * Port specific definitions.
 *
 * The settings in this file configure FreeRTOS correctly for the given hardware
 * and compiler.
 *
 * These settings should not be altered.
 *-----------------------------------------------------------
 */

typedef uint32_t StackType_t;
typedef long BaseType_t;

/*-----------------------------------------------------------*/

/* Hardware specifics. */
#define portBYTE_ALIGNMENT          8

/*-----------------------------------------------------------*/

/* If configUSE_TASK_FPU_SUPPORT is set to 1 (or left undefined) then tasks are
created without an FPU context and must call vPortTaskUsesFPU() to give
themselves an FPU context before using any FPU instructions.  If
configUSE_TASK_FPU_SUPPORT is set to 2 then all tasks will have an FPU context
by default. */
#if( configUSE_TASK_FPU_SUPPORT != 2 )
    void vPortTaskUsesFPU( void );
#else
    /* Each task has an FPU context already, so define this function away to
    nothing to prevent it being called accidentally. */
    #define vPortTaskUsesFPU()
#endif

/* Interrupt controller access addresses. */
#define portICCPMR_PRIORITY_MASK_OFFSET                         ( 0x04 )
#define portICCIAR_INTERRUPT_ACKNOWLEDGE_OFFSET                 ( 0x0C )
#define portICCEOIR_END_OF_INTERRUPT_OFFSET                     ( 0x10 )

#define portINTERRUPT_CONTROLLER_CPU_INTERFACE_ADDRESS      ( configINTERRUPT_CONTROLLER_BASE_ADDRESS + configINTERRUPT_CONTROLLER_CPU_INTERFACE_OFFSET )
#define portICCIAR_INTERRUPT_ACKNOWLEDGE_REGISTER_ADDRESS   ( portINTERRUPT_CONTROLLER_CPU_INTERFACE_ADDRESS + portICCIAR_INTERRUPT_ACKNOWLEDGE_OFFSET )
#define portICCEOIR_END_OF_INTERRUPT_REGISTER_ADDRESS       ( portINTERRUPT_CONTROLLER_CPU_INTERFACE_ADDRESS + portICCEOIR_END_OF_INTERRUPT_OFFSET )

#endif /* PORTMACRO_H */
