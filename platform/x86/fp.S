/* SPDX-License-Identifier: GPL-3.0-or-later */
.text
.globl boot_fp_reset
boot_fp_reset:
    fninit
    sub $8, %rsp
    movl $0x1f80, (%rsp)
    ldmxcsr (%rsp)
    add $8, %rsp
    ret
