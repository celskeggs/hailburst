    .eabi_attribute Tag_ABI_align_preserved, 1
    .text
    .arm

    .set SYS_MODE, 0x1f

    .extern shared_clip_stack
    .extern clip_play_direct

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
