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
    .data : { *(.data*) } :data
    .bss : { *(.bss*) *(COMMON*) } :data
}
