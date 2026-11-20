/* The Amiga program's sections, each linked at its own base so amiga/tools/elf2hunk.py can tell
 * the targets of absolute relocations apart: code and read-only data (hunk 0, any memory), data
 * (hunk 1, any memory; constructors' table from __init_array_start to __init_array_end), BSS (hunk 2,
 * any memory). A program that powers the game on again restores __data_start..__data_end and clears
 * __bss_start..__bss_end; .keep survives that. Chip RAM is allocated at run time by exec. */
ENTRY(_start)
SECTIONS
{
    .text 0x00000000 : { *(.text.start) *(.text .text.*) *(.rodata .rodata.*) . = ALIGN(4); __text_end = .; }
    .data 0x10000000 : {
        __data_start = .;
        *(.data .data.*) *(.sdata .sdata.*)
        . = ALIGN(4);
        __init_array_start = .; KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array)) KEEP(*(.ctors)) __init_array_end = .;
        __data_end = .;
        *(.keep)                /* (kept when the verify build powers the game on again) */
        . = ALIGN(4);
    }
    .bss  0x20000000 (NOLOAD) : { __bss_start = .; *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON) . = ALIGN(4); __bss_end = .; }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.gnu.attributes) }
}
