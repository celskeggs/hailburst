    .eabi_attribute Tag_ABI_align_preserved, 1
    .text
    .arm

    .set SYS_MODE, 0x1f
    .set IRQ_MODE, 0x12

    .extern shared_clip_stack
    .extern clip_play_direct
    .extern schedule_next_clip

.align 4
clip_enter_context:
    .globl clip_enter_context

    /* Change to system mode and enable interrupts. */
    CPSIE   aif, #SYS_MODE

    /* Switch into the shared clip stack. */
    LDR     SP,  =shared_clip_stack+0x1000

    /* Initialize floating-point context to a known state. */
    MOV     R1,  #0x00000000
    VMSR    FPSCR, R1

    /* Pass R0 forward to clip_play_direct. */
    BL      clip_play_direct
    B       abort

/* Note: ordinarily, this is not used. But we have a special mode for testing
   purposes that changes how waits work, and that mode requires this support. */
.align 4
clip_exit_context:
    .globl clip_exit_context

    /* Change to IRQ mode and disable interrupts. */
    CPSID   aif, #IRQ_MODE

    /* Switch into the supervisor stack. */
    LDR sp, =supervisor_stack+0x1000

    /* Jump into scheduler. */
    B       schedule_next_clip
