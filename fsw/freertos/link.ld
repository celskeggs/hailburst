ENTRY (_start)

SECTIONS
{
    . = 0x40000000;
    .text : { *(.text*) *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON*) }
}
