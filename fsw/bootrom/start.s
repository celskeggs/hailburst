.align 4
.globl interrupt_vector_table
interrupt_vector_table:
    b _reset                    @ Reset
    b abort_handler             @ Undefined Instruction
    b abort_handler             @ Supervisor Call (SWI instruction)
    b abort_handler             @ Prefetch Abort
    b abort_handler             @ Data Abort
    b abort_handler             @ (unused)
    b abort_handler             @ IRQ interrupt
    b abort_handler             @ FIQ interrupt

.section .bss

.align 8
.comm stack,       0x1000       @ Reserve 4k stack in the BSS

.section .text

.align 4
_reset:
    cpsid i                     @ Make sure interrupts are disabled

    ldr sp, =stack+0x1000       @ Set up the stack
    bl boot_phase_1             @ Run first entrypoint, which returns stack relocation point

    add sp, r0, #0x1000         @ Relocate stack to the returned address
    bl boot_phase_2             @ Run second entrypoint, which actually loads the kernel

    mov lr, r0                  @ Move aside jump target into a scratch register
    ldr r0, =embedded_kernel    @ Pass address of embedded kernel to itself
    blx lr                      @ Jump into the loaded kernel

    @ If the kernel returns, go into the abort handler.

.align 4
abort_handler:
    ldr sp, =stack+0x1000       @ Reset to known stack state; don't care if we clobber it.
    bl abort_report
    b .
