PHDRS
{
    text PT_LOAD; /* read-and-execute */
    data PT_LOAD; /* read-and-write */
}

FORCE_COMMON_ALLOCATION;

SECTIONS
{
    /* use invalid start location, because this will be relocated later */
    . = 0x20000000;
    .text : { *(.text*) *(.rodata*) } :text
    .initpoints : { *(.initpoints) } :text
    .data : { *(.data*) } :data
    .tasktable : { *(.tasktable) } :data
    .bss : { *(.bss*) *(COMMON*) } :data
    .debugf_messages (INFO) : { *(.debugf_messages) }
}
