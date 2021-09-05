.align 4
interrupt_vector_table:
    b .                         @ Reset
    b .                         @ Undefined Instruction
    b FreeRTOS_SWI_Handler      @ Supervisor Call (SWI instruction)
    b .                         @ Prefetch Abort
    b data_abort_handler        @ Data Abort
    b .                         @ (unused)
    b FreeRTOS_IRQ_Handler      @ IRQ interrupt
    b .                         @ FIQ interrupt

.align 8
.comm stack,     0x1000         @ Reserve 4k stack in the BSS
.align 8
.comm irq_stack, 0x1000         @ Reserve 4k stack in the BSS

.align 4
_start:
    .globl _start

    cps #0x12                   @ Transition to IRQ mode
    ldr sp, =irq_stack+0x1000   @ Set up the IRQ stack

    cps #0x13                   @ Transition to supervisor mode
    ldr sp, =stack+0x1000       @ Set up the supervisor stack

    ldr r0, =interrupt_vector_table
    mcr p15, 0, r0, c12, c0, 0  @ Set up the interrupt vector table in the VBAR register

    bl entrypoint               @ Jump to the entrypoint function

.align 4
data_abort_handler:
	/* Save the LR and SPSR onto the system mode stack before switching to
	system mode to save the remaining system mode registers. */
	srsdb	sp!, #0x1f
	cps		#0x1f
	push	{R0-R12, R14}

    mov    r0, sp
    bl     data_abort_report
