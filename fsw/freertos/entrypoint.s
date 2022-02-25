/*
 * This file is partially borrowed from the FreeRTOS GCC/ARM_CA9 port.
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
    .eabi_attribute Tag_ABI_align_preserved, 1
    .text
    .arm

    .set SYS_MODE, 0x1f
    .set UND_MODE, 0x1B
    .set ABT_MODE, 0x17
    .set SVC_MODE, 0x13
    .set IRQ_MODE, 0x12

    .set IRQ_PPI_BASE, 16 /* replicated from gic.h */
    .set IRQ_PHYS_TIMER, IRQ_PPI_BASE + 14

    /* Hardware registers. */
    .extern ulICCIAR
    .extern ulICCEOIR

    /* Variables and functions. */
    .extern _freertos_vector_table
    .extern pxCurrentTCB
    .extern vTaskSwitchContext
    .extern vApplicationIRQHandler
    .extern ulPortInterruptNesting

    .global FreeRTOS_IRQ_Handler
    .global vPortRestoreTaskContext




.macro SAVE_CONTEXT

    /* Save the LR and SPSR onto the system mode stack before switching to
    system mode to save the remaining system mode registers. */
    SRSDB   sp!, #SYS_MODE
    CPS     #SYS_MODE
    PUSH    {R0-R12, R14}

    /* Save the floating point context. */
    FMRX    R1, FPSCR
    VPUSH   {D0-D15}
    VPUSH   {D16-D31}
    PUSH    {R1}

    /* Save the stack pointer in the TCB. */
    LDR     R0, pxCurrentTCBConst   /* R0 = &pxCurrentTCB                   */
    LDR     R1, [R0]                /* R1 = pxCurrentTCB                    */
    LDR     R2, [R1]                /* R2 = &pxCurrentTCB->mut              */
    STR     SP, [R2]                /* SP = pxCurrentTCB->mut->pxTopOfStack */

    .endm

; /**********************************************************************/

.align 4
resume_restore_context:
    .globl resume_restore_context

    /* Make sure we're in system mode. */
    CPS     #SYS_MODE

    /* Takes as a parameter the stack to switch into. */
    MOV     SP, R0

    /* Restore the floating point context. */
    POP     {R0}
    VPOP    {D16-D31}
    VPOP    {D0-D15}
    VMSR    FPSCR, R0

    /* Restore all system mode registers other than the SP (which is already
    being used). */
    POP     {R0-R12, R14}

    /* Return to the task code, loading CPSR on the way. */
    RFEIA   sp!


.align 5
interrupt_vector_table:
    b .                         @ Reset
    b undef_insn_handler        @ Undefined Instruction
    b supervisor_abort_handler  @ Supervisor Call (SWI instruction)
    b prefetch_abort_handler    @ Prefetch Abort
    b data_abort_handler        @ Data Abort
    b .                         @ (unused)
    b interrupt_handler         @ IRQ interrupt
    b .                         @ FIQ interrupt


.align 8
.comm supervisor_stack, 0x1000  @ Reserve 4k stack in the BSS
.align 8
.comm interrupt_stack, 0x1000   @ Reserve 4k stack in the BSS
.align 8
.comm abort_stack, 0x1000       @ Reserve 4k stack in the BSS


.align 4
_start:                               @ r0 is populated by the bootrom with the ROM address of the kernel ELF file
    .globl _start

    CPS #IRQ_MODE                     @ Transition to IRQ mode
    LDR sp, =interrupt_stack+0x1000   @ Set up the IRQ stack

    CPS #ABT_MODE                     @ Transition to ABORT mode
    LDR sp, =abort_stack+0x1000       @ Set up the ABORT stack (shared with UNDEFINED)

    CPS #UND_MODE                     @ Transition to UNDEFINED mode
    LDR sp, =abort_stack+0x1000       @ Set up the UNDEFINED stack (shared with ABORT)

    CPS #SVC_MODE                     @ Transition to supervisor mode
    LDR sp, =supervisor_stack+0x1000  @ Set up the supervisor stack

    LDR r1, =interrupt_vector_table
    MCR p15, 0, r1, c12, c0, 0        @ Set up the interrupt vector table in the VBAR register

    BL entrypoint                     @ Jump to the entrypoint function, passing r0 (kernel ELF address)

    B  .


/******************************************************************************
 * SVC handler is used to start the scheduler.
 *****************************************************************************/
@ .align 4
@ .type supervisor_call_handler, %function
@ supervisor_call_handler:
@    /* Save the context of the current task and select a new task to run. */
@    SAVE_CONTEXT
@    BL      vTaskSwitchContext
@    RESTORE_CONTEXT

    .data

.align 4
.globl trap_recursive_flag
trap_recursive_flag:
    .word 0

    .text

.macro TRAP_HANDLER trapid

    @ Verify that our trap is NOT recursive
    PUSH    {r12, r14}       @ (Use r12 and r14 as scratch space to simplify emergency_abort_handler.)

    @ Check if the trap is recursive
    LDR     r12, =trap_recursive_flag
    LDR     r14, [r12]
    CMP     r14, #0
    ADD     r14, r14, #1
    STR     r14, [r12]

    @ And also check if we're in a nested interrupt, where we also cannot be safely suspended or restarted
    LDREQ   r14, ulPortInterruptNestingConst
    LDREQ   r14, [r14]
    CMPEQ   r14, #0

    @ If the trap is recursive, OR we're in a critical section, we'll jump to the emergency handler
    @ (more cases below)
    MOV     r14, #\trapid
    BNE     emergency_abort_handler

    @ Next, check whether this particular task is recursively trapping.
    LDR     r12, pxCurrentTCBConst  /* r12 = &pxCurrentTCB */
    LDR     r12, [r12]              /* r12 = pxCurrentTCB */
    CMP     r12, #0      @ If no task, make sure we go to the emergency abort handler!
    BEQ     emergency_abort_handler
    LDR     r12, [r12]              /* r12 = &pxCurrentTCB->mut */
    ADD     r12, #4                 /* r12 = &pxCurrentTCB->mut->recursive_exception */
    LDR     r12, [r12]              /* r12 = pxCurrentTCB->mut->recursive_exception */
    CMP     r12, #0      @ If nonzero, then recursively trapping!
    BNE     emergency_abort_handler

    @ Otherwise, we're not recursive.
    POP     {r12, r14}

    SAVE_CONTEXT
    MOV     r0,  #\trapid
    B       task_abort_handler      @ trap_abort_handler will reset trap_recursive_flag to 0
    @ task_abort_handler does not return

    .endm

.align 4
.type undef_insn_handler, %function
undef_insn_handler:
    TRAP_HANDLER 0

.align 4
.type supervisor_abort_handler, %function
supervisor_abort_handler:
    TRAP_HANDLER 1

.align 4
.type prefetch_abort_handler, %function
prefetch_abort_handler:
    TRAP_HANDLER 2

.align 4
.type data_abort_handler, %function
data_abort_handler:
    TRAP_HANDLER 3

.align 4
.type emergency_abort_handler, %function
emergency_abort_handler:
    PUSH   {r0-r11}           @ r12, r14 already pushed

    MRS    r0, spsr
    MOV    r1, sp
    MOV    r2, r14
    MOV    r11, #0            @ wipe FP to avoid GDB thinking this is an infinite recursive call chain

    LDR    r12, =trap_recursive_flag
    LDR    r12, [r12]
    CMP    r12, #2

    BLLE   exception_report   @ only try to report if this is our first/second time through... otherwise just abort directly
    BL     abort
    B      .

.align 4
.type interrupt_handler, %function
interrupt_handler:
    /* Return to the interrupted instruction. */
    SUB     lr, lr, #4

    /* Push the return address and SPSR. */
    PUSH    {lr}
    MRS     lr, SPSR
    PUSH    {lr}

    /* Change to supervisor mode to allow reentry. */
    CPS     #SVC_MODE

    /* Push used registers. */
    PUSH    {r0-r4, r12}

    /* Increment nesting count.  r3 holds the address of ulPortInterruptNesting
    for future use.  r1 holds the original ulPortInterruptNesting value for
    future use. */
    LDR     r3, ulPortInterruptNestingConst
    LDR     r1, [r3]
    ADD     r4, r1, #1
    STR     r4, [r3]

    /* Read value from the interrupt acknowledge register, which is stored in r0
    for future parameter and interrupt clearing use. */
    LDR     r2, ulICCIARConst
    LDR     r2, [r2]
    LDR     r0, [r2]

    /* Check whether this is a timer interrupt */
    CMP     r0, #IRQ_PHYS_TIMER
    BNE     regular_device_irq

    /****** TIMER INTERRUPT ******/

    /* Write the value read from ICCIAR to ICCEOIR. */
    LDR     r4, ulICCEOIRConst
    LDR     r4, [r4]
    STR     r0, [r4]

    /* Restore the old nesting count. */
    STR     r1, [r3]

    /* Timer interrupts should never nest. */
    CMP     r1, #0
    BNE     abort

    /* Restore used registers, LR-irq and SPSR before saving the context
    to the task stack. */
    POP     {r0-r4, r12}
    CPS     #IRQ_MODE
    POP     {LR}
    MSR     SPSR_cxsf, LR
    POP     {LR}
    SAVE_CONTEXT

    /* Call the function that selects the new task to execute. It will directly
    call resume_restore_context and never return. */
    B       vTaskSwitchContext

    /****** DEVICE INTERRUPT ******/

regular_device_irq:

    /* Ensure bit 2 of the stack pointer is clear.  r2 holds the bit 2 value for
    future use.  _RB_ Does this ever actually need to be done provided the start
    of the stack is 8-byte aligned? */
    MOV     r2, sp
    AND     r2, r2, #4
    SUB     sp, sp, r2

    /* Call the interrupt handler.  r4 pushed to maintain alignment. */
    PUSH    {r0-r4, lr}
    LDR     r1, vApplicationIRQHandlerConst
    BLX     r1
    POP     {r0-r4, lr}
    ADD     sp, sp, r2

    CPSID   i
    DSB
    ISB

    /* Write the value read from ICCIAR to ICCEOIR. */
    LDR     r4, ulICCEOIRConst
    LDR     r4, [r4]
    STR     r0, [r4]

    /* Restore the old nesting count. */
    STR     r1, [r3]

    /* No context switch.  Restore used registers, LR_irq and SPSR before
    returning. */
    POP     {r0-r4, r12}
    CPS     #IRQ_MODE
    POP     {LR}
    MSR     SPSR_cxsf, LR
    POP     {LR}
    MOVS    PC, LR


ulICCIARConst: .word ulICCIAR
ulICCEOIRConst: .word ulICCEOIR
pxCurrentTCBConst: .word pxCurrentTCB
vApplicationIRQHandlerConst: .word vApplicationIRQHandler
ulPortInterruptNestingConst: .word ulPortInterruptNesting

.end
