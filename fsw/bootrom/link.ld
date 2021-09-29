SECTIONS
{
    . = 0x00000000;
    .text : {
        */start.o(.text)
        *(.text*)
        embedded_kernel = .;
        */embedded-kernel.o(.data)
        *(.rodata*)
    }
    . = 0x40000000;
    .bss : { *(.bss*) *(COMMON*) }
}
