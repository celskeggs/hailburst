.align 4
interrupt_vector_table:
    b .                         @ Reset
    b undef_insn_handler        @ Undefined Instruction
    b FreeRTOS_SWI_Handler      @ Supervisor Call (SWI instruction)
    b prefetch_abort_handler    @ Prefetch Abort
    b data_abort_handler        @ Data Abort
    b .                         @ (unused)
    b FreeRTOS_IRQ_Handler      @ IRQ interrupt
    b .                         @ FIQ interrupt

.align 8
.comm stack,     0x1000         @ Reserve 4k stack in the BSS
.align 8
.comm irq_stack, 0x1000         @ Reserve 4k stack in the BSS
.align 8
.comm abort_stack, 0x1000       @ Reserve 4k stack in the BSS

.align 4
_start:                         @ r0 is populated by the bootrom with the ROM address of the kernel ELF file
    .globl _start

    cps #0x12                   @ Transition to IRQ mode
    ldr sp, =irq_stack+0x1000   @ Set up the IRQ stack

    cps #0x17                   @ Transition to ABORT mode
    ldr sp, =abort_stack+0x1000 @ Set up the ABORT stack (shared with UNDEFINED)

    cps #0x1B                   @ Transition to UNDEFINED mode
    ldr sp, =abort_stack+0x1000 @ Set up the UNDEFINED stack (shared with ABORT)

    cps #0x13                   @ Transition to supervisor mode
    ldr sp, =stack+0x1000       @ Set up the supervisor stack

    ldr r1, =interrupt_vector_table
    mcr p15, 0, r1, c12, c0, 0  @ Set up the interrupt vector table in the VBAR register

    bl entrypoint               @ Jump to the entrypoint function, passing r0 (kernel ELF address)

.align 4
undef_insn_handler:
    push   {r0-r12, r14}
    mrs    r0, spsr
    mov    r1, sp
    mov    r2, #0
    bl     exception_report

.align 4
prefetch_abort_handler:
    push   {r0-r12, r14}
    mrs    r0, spsr
    mov    r1, sp
    mov    r2, #1
    bl     exception_report

.align 4
data_abort_handler:
    push   {r0-r12, r14}
    mrs    r0, spsr
    mov    r1, sp
    mov    r2, #2
    bl     exception_report
