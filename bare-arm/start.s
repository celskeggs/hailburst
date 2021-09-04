interrupt_vector_table:
    b .                         @ Reset
    b .                         @ Undefined Instruction
    b FreeRTOS_SWI_Handler      @ Supervisor Call (SWI instruction)
    b .                         @ Prefetch Abort
    b .                         @ Data Abort
    b .                         @ (unused)
    b FreeRTOS_IRQ_Handler      @ IRQ interrupt
    b .                         @ FIQ interrupt

.comm stack,     0x1000         @ Reserve 4k stack in the BSS
.comm irq_stack, 0x1000         @ Reserve 4k stack in the BSS
_start:
    .globl _start

    cps #0x12                   @ Transition to IRQ mode
    ldr sp, =irq_stack+0x1000   @ Set up the IRQ stack

    cps #0x13                   @ Transition to supervisor mode
    ldr sp, =stack+0x1000       @ Set up the supervisor stack

    ldr r0, =interrupt_vector_table
    mcr p15, 0, r0, c12, c0, 0  @ Set up the interrupt vector table in the VBAR register

    bl entrypoint               @ Jump to the entrypoint function
