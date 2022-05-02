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

    .set MODE_MASK, 0x1f
    .set SYS_MODE, 0x1f
    .set UND_MODE, 0x1B
    .set ABT_MODE, 0x17
    .set SVC_MODE, 0x13
    .set IRQ_MODE, 0x12

    /* Variables and functions. */
    .extern schedule_current_clip
    .extern gic_interrupt_handler
    .extern trap_recursive_flag


; /**********************************************************************/


.align 8
.comm supervisor_stack, 0x1000  @ Reserve 4k stack in the BSS
.align 8
.comm shared_clip_stack, 0x1000 @ Reserve 4k stack in the BSS (this is used in clip_entry.s)
.align 8
.comm trap_stack, 0x1000        @ Reserve 4k stack in the BSS

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


.align 4
_start:                               @ r0 is populated by the bootrom with the ROM address of the kernel ELF file
    .globl _start

    CPS #IRQ_MODE                     @ Transition to IRQ mode, which is the mode used in the scheduler.
    LDR sp, =supervisor_stack+0x1000

    LDR r1, =interrupt_vector_table
    MCR p15, 0, r1, c12, c0, 0        @ Set up the interrupt vector table in the VBAR register

    BL entrypoint                     @ Jump to the entrypoint function, passing r0 (kernel ELF address)

    B  .

.macro TRAP_HANDLER trapid
    @ Set up the trap handling stack
    LDR sp, =trap_stack+0x1000

    @ Verify that our trap is NOT recursive
    PUSH    {r12, r14}       @ (Use r12 and r14 as scratch space to simplify emergency_abort_handler.)

    @ Check if the trap is recursive
    LDR     r12, =trap_recursive_flag
    LDR     r14, [r12]
    CMP     r14, #0
    ADD     r14, r14, #1
    STR     r14, [r12]

    @ And also check if we were in the kernel, where we also cannot be safely suspended or restarted
    MRSEQ   r14, SPSR
    ANDEQ   r14, r14, #MODE_MASK
    CMPEQ   r14, #SYS_MODE

    @ If the trap is recursive, OR we're in a critical section, we'll jump to the emergency handler
    @ (more cases below)
    MOV     r14, #\trapid
    BNE     emergency_abort_handler

    @ Next, check whether this particular task is recursively trapping.
    LDR     r12, =schedule_current_clip  /* r12 = &schedule_current_clip */
    LDR     r12, [r12]                   /* r12 = schedule_current_clip */
    CMP     r12, #0      @ If no task, make sure we go to the emergency abort handler!
    BEQ     emergency_abort_handler
    LDR     r12, [r12]                   /* r12 = &schedule_current_clip->mut->recursive_exception */
    LDR     r12, [r12]                   /* r12 = schedule_current_clip->mut->recursive_exception */
    CMP     r12, #0      @ If nonzero, then recursively trapping!
    BNE     emergency_abort_handler

    @ Otherwise, we're not recursive.
    POP     {r12, r14}

    CPS     #SYS_MODE

    MOV     r0,  #\trapid
    B       clip_abort_handler      @ trap_abort_handler will reset trap_recursive_flag to 0
    @ clip_abort_handler does not return

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
    /* Use the supervisor stack. */
    LDR     sp, =supervisor_stack+0x1000

    /* Jump into C code. */
    B       gic_interrupt_handler

.end
