/* SPDX-License-Identifier: GPL-3.0-or-later */
/* BL-only INT 13h thunk; copied to a reserved, aligned low 64-KiB region.
 * Entry requires flat protected mode and paging off. Full eager FP is saved. */
.section .rodata.disk_thunk,"a"
.code16
.globl boot_disk_thunk_start,boot_disk_thunk_end,boot_disk_real_offset
boot_disk_thunk_start:
    movw $32,%ax
    movw %ax,%ds
    movw %ax,%es
    movl %cr0,%eax
    andl $~1,%eax
    movl %eax,%cr0
    ljmpw *%cs:0x810
real:
    movw %cs,%ax
    movw %ax,%ds
    movw %ax,%es
    movw %ax,%ss
    movl $0xfff0,%esp
    lidt %cs:0x848
    movl %cs:0xa00,%eax
    movl %cs:0xa04,%ebx
    movl %cs:0xa08,%ecx
    movl %cs:0xa0c,%edx
    movl %cs:0xa10,%esi
    movl %cs:0xa14,%edi
    stc
    sti
    int $0x13
    pushfl
    cli
    popl %cs:0xa18
    movl %eax,%cs:0xa00
    movl %ebx,%cs:0xa04
    movl %ecx,%cs:0xa08
    movl %edx,%cs:0xa0c
    movl %esi,%cs:0xa10
    movl %edi,%cs:0xa14
    movl %cr0,%eax
    orl $1,%eax
    movl %eax,%cr0
    ljmpl *%cs:0x818
.set boot_disk_real_offset,real-boot_disk_thunk_start
boot_disk_thunk_end:
.text
.code32
.globl boot_disk_call,boot_disk_return
boot_disk_call:
    pushfl
    cli
    pushal
    mov 40(%esp),%ebx
    mov %esp,0x820(%ebx)
    sgdt 0x828(%ebx)
    sidt 0x840(%ebx)
    mov %cr0,%eax
    mov %eax,0x850(%ebx)
    mov %cr3,%eax
    mov %eax,0x854(%ebx)
    mov %cr4,%eax
    mov %eax,0x858(%ebx)
    fxsave 0xc00(%ebx)
    mov %ebx,disk_base
    lgdt 0x830(%ebx)
    ljmp *0x838(%ebx)
boot_disk_return:
    mov $16,%eax
    mov %ax,%ds
    mov %ax,%es
    mov %ax,%ss
    mov %ax,%fs
    mov %ax,%gs
    mov disk_base,%ebx
    lgdt 0x828(%ebx)
    lidt 0x840(%ebx)
    mov 0x858(%ebx),%eax
    mov %eax,%cr4
    mov 0x854(%ebx),%eax
    mov %eax,%cr3
    mov 0x850(%ebx),%eax
    mov %eax,%cr0
    fxrstor 0xc00(%ebx)
    /* Compare the actual eager FP state after every firmware invocation. */
    fxsave 0xe00(%ebx)
    lea 0xc00(%ebx),%esi
    lea 0xe00(%ebx),%edi
    mov $512,%ecx
    cld
    repe cmpsb
    sete 0xa1c(%ebx)
    mov 0x820(%ebx),%esp
    popal
    popfl
    ret
.bss
.balign 4
disk_base: .long 0
.section .note.GNU-stack,"",@progbits
