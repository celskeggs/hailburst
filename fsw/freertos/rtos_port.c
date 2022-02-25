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

/* Standard includes. */
#include <stdlib.h>

/* Scheduler includes. */
#include <FreeRTOS.h>
#include <task.h>

#ifndef configINTERRUPT_CONTROLLER_BASE_ADDRESS
    #error configINTERRUPT_CONTROLLER_BASE_ADDRESS must be defined.  See https://www.FreeRTOS.org/Using-FreeRTOS-on-Cortex-A-Embedded-Processors.html
#endif

#ifndef configINTERRUPT_CONTROLLER_CPU_INTERFACE_OFFSET
    #error configINTERRUPT_CONTROLLER_CPU_INTERFACE_OFFSET must be defined.  See https://www.FreeRTOS.org/Using-FreeRTOS-on-Cortex-A-Embedded-Processors.html
#endif

/* Tasks are not created with a floating point context, but can be given a
floating point context after they have been created.  A variable is stored as
part of the tasks context that holds portNO_FLOATING_POINT_CONTEXT if the task
does not have an FPU context, or any other value if the task does have an FPU
context. */
#define portNO_FLOATING_POINT_CONTEXT   ( ( StackType_t ) 0 )

/* Constants required to setup the initial task context. */
#define portINITIAL_SPSR                ( ( StackType_t ) 0x1f ) /* System mode, ARM mode, IRQ enabled FIQ enabled. */\

/* The space on the stack required to hold the FPU registers.  This is 32 64-bit
registers, plus a 32-bit status register. */
#define portFPU_REGISTER_WORDS  ( ( 32 * 2 ) + 1 )

/*-----------------------------------------------------------*/

/* Counts the interrupt nesting depth.  A context switch is only performed if
if the nesting depth is 0. */
volatile uint32_t ulPortInterruptNesting = 0UL;

/* Used in the asm file. */
__attribute__(( used )) const uint32_t ulICCIAR = portICCIAR_INTERRUPT_ACKNOWLEDGE_REGISTER_ADDRESS;
__attribute__(( used )) const uint32_t ulICCEOIR = portICCEOIR_END_OF_INTERRUPT_REGISTER_ADDRESS;

/*-----------------------------------------------------------*/

// available in thread.c
extern void task_entrypoint( TCB_t * task );

/*
 * See header file for description.
 */
StackType_t *pxPortInitialiseStack( StackType_t *pxTopOfStack, TCB_t * pxNewTCB )
{
    /* Setup the initial stack of the task.  The stack is set exactly as
    expected by the portRESTORE_CONTEXT() macro.

    The fist real value on the stack is the status register, which is set for
    system mode, with interrupts enabled.  A few NULLs are added first to ensure
    GDB does not try decoding a non-existent return address. */
    *pxTopOfStack = ( StackType_t ) NULL;
    pxTopOfStack--;
    *pxTopOfStack = ( StackType_t ) NULL;
    pxTopOfStack--;
    *pxTopOfStack = ( StackType_t ) NULL;
    pxTopOfStack--;
    *pxTopOfStack = ( StackType_t ) portINITIAL_SPSR;
    pxTopOfStack--;

    /* Next the return address, which in this case is the start of the task. */
    *pxTopOfStack = ( StackType_t ) task_entrypoint;
    pxTopOfStack--;

    /* Next all the registers other than the stack pointer. */
    *pxTopOfStack = ( StackType_t ) abort; /* R14 */
    pxTopOfStack--;
    *pxTopOfStack = ( StackType_t ) 0x12121212; /* R12 */
    pxTopOfStack--;
    *pxTopOfStack = ( StackType_t ) 0x11111111; /* R11 */
    pxTopOfStack--;
    *pxTopOfStack = ( StackType_t ) 0x10101010; /* R10 */
    pxTopOfStack--;
    *pxTopOfStack = ( StackType_t ) 0x09090909; /* R9 */
    pxTopOfStack--;
    *pxTopOfStack = ( StackType_t ) 0x08080808; /* R8 */
    pxTopOfStack--;
    *pxTopOfStack = ( StackType_t ) 0x07070707; /* R7 */
    pxTopOfStack--;
    *pxTopOfStack = ( StackType_t ) 0x06060606; /* R6 */
    pxTopOfStack--;
    *pxTopOfStack = ( StackType_t ) 0x05050505; /* R5 */
    pxTopOfStack--;
    *pxTopOfStack = ( StackType_t ) 0x04040404; /* R4 */
    pxTopOfStack--;
    *pxTopOfStack = ( StackType_t ) 0x03030303; /* R3 */
    pxTopOfStack--;
    *pxTopOfStack = ( StackType_t ) 0x02020202; /* R2 */
    pxTopOfStack--;
    *pxTopOfStack = ( StackType_t ) 0x01010101; /* R1 */
    pxTopOfStack--;
    *pxTopOfStack = ( StackType_t ) pxNewTCB; /* R0 */

    /* The task will start with a floating point context.  Leave enough
    space for the registers - and ensure they are initialised to 0. */
    pxTopOfStack -= portFPU_REGISTER_WORDS;
    memset( pxTopOfStack, 0x00, portFPU_REGISTER_WORDS * sizeof( StackType_t ) );

    return pxTopOfStack;
}
