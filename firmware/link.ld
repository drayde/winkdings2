/*
 * Linker script for CH32V203G6 (CH32V203G6U6).
 * FLASH: 32K, RAM: 10K -- adjust if you use another variant
 * (e.g. *8*: 64K/20K, *B*: 128K/64K).
 */

ENTRY(_start)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 32K
    RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 10K
}

SECTIONS
{
    .init :
    {
        . = ALIGN(4);
        KEEP(*(.init))
        . = ALIGN(4);
    } >FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
        *(.srodata.*)
        . = ALIGN(4);
    } >FLASH

    .data :
    {
        . = ALIGN(4);
        PROVIDE(_data_vma = .);
        *(.gnu.linkonce.r.*)
        *(.data .data.*)
        *(.gnu.linkonce.d.*)
        . = ALIGN(8);
        PROVIDE(__global_pointer$ = . + 0x800);
        *(.sdata .sdata.*)
        *(.sdata2*)
        *(.gnu.linkonce.s.*)
        . = ALIGN(8);
        *(.srodata.cst16)
        *(.srodata.cst8)
        *(.srodata.cst4)
        *(.srodata.cst2)
        *(.srodata .srodata.*)
        . = ALIGN(4);
        PROVIDE(_edata = .);
    } >RAM AT>FLASH
    _data_lma = LOADADDR(.data);

    .bss :
    {
        . = ALIGN(4);
        PROVIDE(_sbss = .);
        *(.sbss*)
        *(.gnu.linkonce.sb.*)
        *(.bss .bss.*)
        *(.gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(4);
        PROVIDE(_ebss = .);
    } >RAM

    /DISCARD/ :
    {
        *(.eh_frame*)
        *(.comment)
        *(.note*)
    }

    . = ALIGN(4);
    PROVIDE(_end = .);
    PROVIDE(end = .);
    PROVIDE(_eusrstack = ORIGIN(RAM) + LENGTH(RAM));
}
