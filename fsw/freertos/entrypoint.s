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

	.set SYS_MODE,	0x1f
	.set UND_MODE,	0x1B
	.set ABT_MODE,	0x17
	.set SVC_MODE,	0x13
	.set IRQ_MODE,	0x12

	/* Hardware registers. */
	.extern ulICCIAR
	.extern ulICCEOIR
	.extern ulICCPMR

	/* Variables and functions. */
	.extern ulMaxAPIPriorityMask
	.extern _freertos_vector_table
	.extern pxCurrentTCB
	.extern vTaskSwitchContext
	.extern vApplicationIRQHandler
	.extern ulPortInterruptNesting
	.extern ulPortTaskHasFPUContext

	.global FreeRTOS_IRQ_Handler
	.global FreeRTOS_SWI_Handler
	.global vPortRestoreTaskContext




.macro SAVE_CONTEXT

	/* Save the LR and SPSR onto the system mode stack before switching to
	system mode to save the remaining system mode registers. */
	SRSDB	sp!, #SYS_MODE
	CPS		#SYS_MODE
	PUSH	{R0-R12, R14}

	/* Push the critical nesting count. */
	LDR		R2, ulCriticalNestingConst
	LDR		R1, [R2]
	PUSH	{R1}

	/* Does the task have a floating point context that needs saving?  If
	ulPortTaskHasFPUContext is 0 then no. */
	LDR		R2, ulPortTaskHasFPUContextConst
	LDR		R3, [R2]
	CMP		R3, #0

	/* Save the floating point context, if any. */
	FMRXNE  R1,  FPSCR
	VPUSHNE {D0-D15}
	VPUSHNE	{D16-D31}
	PUSHNE	{R1}

	/* Save ulPortTaskHasFPUContext itself. */
	PUSH	{R3}

	/* Save the stack pointer in the TCB. */
	LDR		R0, pxCurrentTCBConst
	LDR		R1, [R0]
	STR		SP, [R1]

	.endm

; /**********************************************************************/

.macro RESTORE_CONTEXT

	/* Set the SP to point to the stack of the task being restored. */
	LDR		R0, pxCurrentTCBConst
	LDR		R1, [R0]
	LDR		SP, [R1]

	/* Is there a floating point context to restore?  If the restored
	ulPortTaskHasFPUContext is zero then no. */
	LDR		R0, ulPortTaskHasFPUContextConst
	POP		{R1}
	STR		R1, [R0]
	CMP		R1, #0

	/* Restore the floating point context, if any. */
	POPNE 	{R0}
	VPOPNE	{D16-D31}
	VPOPNE	{D0-D15}
	VMSRNE  FPSCR, R0

	/* Restore the critical section nesting depth. */
	LDR		R0, ulCriticalNestingConst
	POP		{R1}
	STR		R1, [R0]

	/* Ensure the priority mask is correct for the critical nesting depth. */
	LDR		R2, ulICCPMRConst
	LDR		R2, [R2]
	CMP		R1, #0
	MOVEQ	R4, #255
	LDRNE	R4, ulMaxAPIPriorityMaskConst
	LDRNE	R4, [R4]
	STR		R4, [R2]

	/* Restore all system mode registers other than the SP (which is already
	being used). */
	POP		{R0-R12, R14}

	/* Return to the task code, loading CPSR on the way. */
	RFEIA	sp!

	.endm


.align 4
interrupt_vector_table:
    b .                         @ Reset
    b undef_insn_handler        @ Undefined Instruction
    b supervisor_call_handler   @ Supervisor Call (SWI instruction)
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
.align 4
.type supervisor_call_handler, %function
supervisor_call_handler:
	/* Save the context of the current task and select a new task to run. */
	SAVE_CONTEXT
	BL      vTaskSwitchContext
	RESTORE_CONTEXT

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

    @ If it is, we'll jump to the emergency handler, and will need to know what type of trap this was
    MOV     r14, #\trapid
    BNE     emergency_abort_handler

    @ Otherwise, we're not recursive.
    POP     {r12, r14}

    SAVE_CONTEXT
    MOV     r0,  #\trapid
    BL      task_abort_handler      @ trap_abort_handler will reset trap_recursive_flag to 0
    RESTORE_CONTEXT

    .endm

.align 4
.type undef_insn_handler, %function
undef_insn_handler:
    TRAP_HANDLER 0

.align 4
.type prefetch_abort_handler, %function
prefetch_abort_handler:
    TRAP_HANDLER 1

.align 4
.type data_abort_handler, %function
data_abort_handler:
    TRAP_HANDLER 2

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
    CMP    r12, #1

    BLEQ   exception_report   @ only try to report if this is our first time through... otherwise just abort directly
    BL     abort
    B      .

/******************************************************************************
 * vPortRestoreTaskContext is used to start the scheduler.
 *****************************************************************************/
.type vPortRestoreTaskContext, %function
vPortRestoreTaskContext:
	/* Switch to system mode. */
	CPS		#SYS_MODE
	RESTORE_CONTEXT

.align 4
.type interrupt_handler, %function
interrupt_handler:
	/* Return to the interrupted instruction. */
	SUB		lr, lr, #4

	/* Push the return address and SPSR. */
	PUSH	{lr}
	MRS		lr, SPSR
	PUSH	{lr}

	/* Change to supervisor mode to allow reentry. */
	CPS		#SVC_MODE

	/* Push used registers. */
	PUSH	{r0-r4, r12}

	/* Increment nesting count.  r3 holds the address of ulPortInterruptNesting
	for future use.  r1 holds the original ulPortInterruptNesting value for
	future use. */
	LDR		r3, ulPortInterruptNestingConst
	LDR		r1, [r3]
	ADD		r4, r1, #1
	STR		r4, [r3]

	/* Read value from the interrupt acknowledge register, which is stored in r0
	for future parameter and interrupt clearing use. */
	LDR 	r2, ulICCIARConst
	LDR		r2, [r2]
	LDR		r0, [r2]

	/* Ensure bit 2 of the stack pointer is clear.  r2 holds the bit 2 value for
	future use.  _RB_ Does this ever actually need to be done provided the start
	of the stack is 8-byte aligned? */
	MOV		r2, sp
	AND		r2, r2, #4
	SUB		sp, sp, r2

	/* Call the interrupt handler.  r4 pushed to maintain alignment. */
	PUSH	{r0-r4, lr}
	LDR		r1, vApplicationIRQHandlerConst
	BLX		r1
	POP		{r0-r4, lr}
	ADD		sp, sp, r2

	CPSID	i
	DSB
	ISB

	/* Write the value read from ICCIAR to ICCEOIR. */
	LDR 	r4, ulICCEOIRConst
	LDR		r4, [r4]
	STR		r0, [r4]

	/* Restore the old nesting count. */
	STR		r1, [r3]

	/* A context switch is never performed if the nesting count is not 0. */
	CMP		r1, #0
	BNE		exit_without_switch

	/* Did the interrupt request a context switch?  r1 holds the address of
	ulPortYieldRequired and r0 the value of ulPortYieldRequired for future
	use. */
	LDR		r1, =ulPortYieldRequired
	LDR		r0, [r1]
	CMP		r0, #0
	BNE		switch_before_exit

exit_without_switch:
	/* No context switch.  Restore used registers, LR_irq and SPSR before
	returning. */
	POP		{r0-r4, r12}
	CPS		#IRQ_MODE
	POP		{LR}
	MSR		SPSR_cxsf, LR
	POP		{LR}
	MOVS	PC, LR

switch_before_exit:
	/* A context swtich is to be performed.  Clear the context switch pending
	flag. */
	MOV		r0, #0
	STR		r0, [r1]

	/* Restore used registers, LR-irq and SPSR before saving the context
	to the task stack. */
	POP		{r0-r4, r12}
	CPS		#IRQ_MODE
	POP		{LR}
	MSR		SPSR_cxsf, LR
	POP		{LR}
	SAVE_CONTEXT

	/* Call the function that selects the new task to execute.
	vTaskSwitchContext() if vTaskSwitchContext() uses LDRD or STRD
	instructions, or 8 byte aligned stack allocated data.  LR does not need
	saving as a new LR will be loaded by RESTORE_CONTEXT anyway. */
	BL      vTaskSwitchContext

	/* Restore the context of, and branch to, the task selected to execute
	next. */
	RESTORE_CONTEXT


ulICCIARConst:	.word ulICCIAR
ulICCEOIRConst:	.word ulICCEOIR
ulICCPMRConst: .word ulICCPMR
pxCurrentTCBConst: .word pxCurrentTCB
ulCriticalNestingConst: .word ulCriticalNesting
ulPortTaskHasFPUContextConst: .word ulPortTaskHasFPUContext
ulMaxAPIPriorityMaskConst: .word ulMaxAPIPriorityMask
vApplicationIRQHandlerConst: .word vApplicationIRQHandler
ulPortInterruptNestingConst: .word ulPortInterruptNesting

.end
