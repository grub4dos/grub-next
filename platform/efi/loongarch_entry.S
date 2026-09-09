/* SPDX-License-Identifier: GPL-3.0-or-later */
.text
.globl efi_main
.type efi_main, @function
efi_main:
    /* EDK2 may enter with EUEN.FPE clear. Enable eager FP before any C/LP64D. */
    csrrd $t0, 0x2
    ori $t0, $t0, 1
    csrwr $t0, 0x2
    movgr2fcsr $fcsr0, $zero
    addi.d $t0, $zero, -16
    and $sp, $sp, $t0
    bl boot_efi_main
1:
    idle 0
    b 1b
.size efi_main, .-efi_main
.section .note.GNU-stack,"",@progbits
