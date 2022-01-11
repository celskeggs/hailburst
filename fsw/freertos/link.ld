ENTRY (_start)

PHDRS
{
    text PT_LOAD; /* read-and-execute */
    data PT_LOAD; /* read-and-write */
}

SECTIONS
{
    . = 0x40000000;
    .text : { *(.text*) *(.rodata*) } :text
    .initpoints : {
        initpoints_start = .;
        *(.initpoints);
        initpoints_end = .;
    } :text
    .data : { *(.data*) } :data
    .tasktable : {
        tasktable_start = .;
        *(.tasktable);
        tasktable_end = .;
    } :data
    .bss : { *(.bss*) *(COMMON*) } :data
    . = 0xF8000000;
    .debugf_messages (INFO) : { *(.debugf_messages) }
}
