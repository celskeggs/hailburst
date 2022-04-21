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
    .extern pxCurrentTCB
    .extern vTaskSwitchContext
    .extern vivid_port_in_kernel


; /**********************************************************************/

.align 4
start_clip_context:
    .globl start_clip_context

    /* Make sure we're in system mode. */
    CPS     #SYS_MODE

    /* Takes as a parameter the stack to switch into (R0). */
    MOV     SP,    R0

    /* Make sure that we were previously in the kernel. */
    LDR     r3, =vivid_port_in_kernel
    LDR     r1, [r3]
    CMP     r1, #1
    BNE     abort

    /* And mark that we are no longer. */
    MOV     r1, #0
    STR     r1, [r3]

    /* Initialize the system mode registers to a known state. */
    MOV     R0,  #0x00000000
    LDR     R1,  =0x01010101
    LDR     R2,  =0x02020202
    LDR     R3,  =0x03030303
    LDR     R4,  =0x04040404
    LDR     R5,  =0x05050505
    LDR     R6,  =0x06060606
    LDR     R7,  =0x07070707
    LDR     R8,  =0x08080808
    LDR     R9,  =0x09090909
    LDR     R10, =0x10101010
    LDR     R11, =0x11111111
    LDR     R12, =0x12121212
    LDR     R14, =abort

    /* Don't bother fully initializing the floating-point context. There's no need for a hard security boundary. */
    VMSR    FPSCR, R0

    /* Already in system mode, but make sure to enable everything before we jump to the entrypoint. */
    CPSIE   aif

    B       clip_play_direct


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

    @ And also check if we're in the kernel, where we also cannot be safely suspended or restarted
    LDREQ   r14, =vivid_port_in_kernel
    LDREQ   r14, [r14]
    CMPEQ   r14, #0

    @ If the trap is recursive, OR we're in a critical section, we'll jump to the emergency handler
    @ (more cases below)
    MOV     r14, #\trapid
    BNE     emergency_abort_handler

    @ Next, check whether this particular task is recursively trapping.
    LDR     r12, =pxCurrentTCB      /* r12 = &pxCurrentTCB */
    LDR     r12, [r12]              /* r12 = pxCurrentTCB */
    CMP     r12, #0      @ If no task, make sure we go to the emergency abort handler!
    BEQ     emergency_abort_handler
    LDR     r12, [r12]              /* r12 = &pxCurrentTCB->mut->recursive_exception */
    LDR     r12, [r12]              /* r12 = pxCurrentTCB->mut->recursive_exception */
    CMP     r12, #0      @ If nonzero, then recursively trapping!
    BNE     emergency_abort_handler

    @ Otherwise, we're not recursive.
    POP     {r12, r14}

    CPS     #SYS_MODE

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
    /* Change to supervisor mode to allow reentry. */
    CPS     #SYS_MODE

    /* Make sure that we weren't already in the kernel. */
    LDR     r3, =vivid_port_in_kernel
    LDR     r1, [r3]
    CMP     r1, #0
    BNE     abort

    /* And mark that we now are. */
    MOV     r1, #1
    STR     r1, [r3]

    /* Read value from the interrupt acknowledge register, which is stored in r0
    for future parameter and interrupt clearing use. */
    LDR     r2, =ulICCIAR
    LDR     r2, [r2]
    LDR     r0, [r2]

    /* Should only encounter timer interrupts; anything else could throw off the partition scheduler! */
    CMP     r0, #IRQ_PHYS_TIMER
    BNE     abort

    /****** TIMER INTERRUPT ******/

    /* Write the value read from ICCIAR to ICCEOIR. */
    LDR     r4, =ulICCEOIR
    LDR     r4, [r4]
    STR     r0, [r4]

    /* Call the function that selects the new task to execute. It will directly
    call resume_restore_context and never return. */
    B       vTaskSwitchContext

.end
