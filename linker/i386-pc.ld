/* SPDX-License-Identifier: GPL-3.0-or-later */
OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 1M;
    .multiboot2 : { KEEP(*(.multiboot2)) } :text
    .text : { *(.text.start) *(.text*) } :text
    .rodata : { *(.rodata*) } :text
    . = ALIGN(4096);
    .data : { *(.data*) } :data
    .bss : { *(.bss*) *(COMMON) } :data
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
ASSERT(SIZEOF(.multiboot2) == 24, "invalid Multiboot2 header")
