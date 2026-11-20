/* The Amiga program's sections, each linked at its own base so amiga/tools/elf2hunk.py can tell
 * the targets of absolute relocations apart: code and read-only data (hunk 0, any memory), data
 * (hunk 1, any memory), BSS (hunk 2, any memory). Chip RAM is allocated at run time by exec. */
ENTRY(_start)
SECTIONS
{
    .text 0x00000000 : { *(.text.start) *(.text .text.*) *(.rodata .rodata.*) . = ALIGN(4); }
    .data 0x10000000 : { *(.data .data.*) *(.sdata .sdata.*) . = ALIGN(4); }
    .bss  0x20000000 (NOLOAD) : { *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON) . = ALIGN(4); }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.gnu.attributes) }
}
