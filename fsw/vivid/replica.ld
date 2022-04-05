PHDRS
{
    text PT_LOAD; /* read-and-execute */
}

FORCE_COMMON_ALLOCATION;

SECTIONS
{
    /* use invalid start location, because this will be relocated later */
    . = 0x20000000;
    .text : { *(.text*) *(.rodata*) } :text
    /DISCARD/ : { *(.data*) *(.bss*) *(COMMON*) *(initpoints) *(tasktable) *(replicas) }
    debugf_messages (INFO) : { *(debugf_messages) }
}
