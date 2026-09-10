/* SPDX-License-Identifier: GPL-3.0-or-later */
OUTPUT_ARCH(loongarch)
ENTRY(efi_main)
SECTIONS {
    . = 0x1000;
    .text : { *(.text*) }
    . = ALIGN(4096);
    .rodata : { *(.rodata*) }
    . = ALIGN(4096);
    .bootres : { KEEP(*(.bootres)) }
    . = ALIGN(4096);
    .data : { *(.data*) *(.got*) }
    .rela.dyn : { *(.rela.dyn) }
    .dynamic : { *(.dynamic) }
    .dynsym : { *(.dynsym) }
    .dynstr : { *(.dynstr) }
    .hash : { *(.hash) *(.gnu.hash) }
    .bss : { *(.bss*) *(COMMON) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
