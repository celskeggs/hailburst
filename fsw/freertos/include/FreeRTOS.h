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
#include <stdint.h>
#include <string.h>

#include <hal/debug.h>

typedef uint32_t StackType_t;

/* Interrupt controller access addresses. */
#define portICCPMR_PRIORITY_MASK_OFFSET                         ( 0x04 )
#define portICCIAR_INTERRUPT_ACKNOWLEDGE_OFFSET                 ( 0x0C )
#define portICCEOIR_END_OF_INTERRUPT_OFFSET                     ( 0x10 )

/* Interrupt nesting behaviour configuration. */
#define configINTERRUPT_CONTROLLER_BASE_ADDRESS         0x08000000
#define configINTERRUPT_CONTROLLER_CPU_INTERFACE_OFFSET 0x00010000

#define portINTERRUPT_CONTROLLER_CPU_INTERFACE_ADDRESS      ( configINTERRUPT_CONTROLLER_BASE_ADDRESS + configINTERRUPT_CONTROLLER_CPU_INTERFACE_OFFSET )
#define portICCIAR_INTERRUPT_ACKNOWLEDGE_REGISTER_ADDRESS   ( portINTERRUPT_CONTROLLER_CPU_INTERFACE_ADDRESS + portICCIAR_INTERRUPT_ACKNOWLEDGE_OFFSET )
#define portICCEOIR_END_OF_INTERRUPT_REGISTER_ADDRESS       ( portINTERRUPT_CONTROLLER_CPU_INTERFACE_ADDRESS + portICCEOIR_END_OF_INTERRUPT_OFFSET )

/* ARM is 8-byte aligned */
#define portBYTE_ALIGNMENT_MASK     ( 0x0007 )

/*
 * Setup the stack of a new task so it is ready to be placed under the
 * scheduler control.  The registers have to be placed on the stack in
 * the order that the port expects to find them.
 *
 */
typedef struct TCB_st const TCB_t;
StackType_t * pxPortInitialiseStack( StackType_t * pxTopOfStack,
                                     TCB_t * pxNewTCB );

#endif /* INC_FREERTOS_H */
