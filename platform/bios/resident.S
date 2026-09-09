/* SPDX-License-Identifier: GPL-3.0-or-later */
.section .rodata.resident,"a"
.code16
.globl boot_resident_start, boot_resident_end
boot_resident_start:
    cmpl $0xe820, %eax
    jne chain
    cmpl $0x534d4150, %edx
    jne chain
    cmpl $20, %ecx
    jb error
    cmpl %cs:0x0800, %ebx
    jae error
    pushw %bp
    movw %sp, %bp
    pushw %si
    pushw %di
    pushw %ds
    pushw %cs
    popw %ds
    imulw $24, %bx, %si
    addw $0x1000, %si
    movl $20, %eax
    cmpl $24, %ecx
    jb 1f
    movl $24, %eax
1:
    movl %eax, %ecx
    rep movsb
    movl %eax, %ecx
    incl %ebx
    cmpl 0x0800, %ebx
    jb 2f
    xorl %ebx, %ebx
2:
    movl $0x534d4150, %eax
    popw %ds
    popw %di
    popw %si
    andw $0xfffe, 6(%bp)
    popw %bp
    iret
error:
    pushw %bp
    movw %sp, %bp
    orw $1, 6(%bp)
    popw %bp
    iret
chain:
    ljmpw *%cs:0x0804
.org 0x100
probe16:
    movw $32, %ax
    movw %ax, %ds
    movw %ax, %es
    movl %cr0, %eax
    andl $0xfffffffe, %eax
    movl %eax, %cr0
    ljmpw *%cs:0x0810
real:
    movw %cs, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movl $0xfff0, %esp
    lidt %cs:0x0848
    xorl %ebx, %ebx
    movl $0, 0x0f00
    movw $0x4000, %di
3:
    movl $0xe820, %eax
    movl $0x534d4150, %edx
    movl $24, %ecx
    int $0x15
    jc 4f
    cmpl $0x534d4150, %eax
    jne 4f
    cmpl $24, %ecx
    jne 4f
    incl 0x0f00
    addw $24, %di
    cmpl $256, 0x0f00
    jae 4f
    testl %ebx, %ebx
    jnz 3b
    jmp 5f
4:
    movl $0xffffffff, 0x0f00
5:
    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0
    ljmpl *%cs:0x0818
.globl boot_resident_real_offset
.set boot_resident_real_offset, real-boot_resident_start
boot_resident_end:

.text
.code32
.globl boot_e820_probe
boot_e820_probe:
    push %ebp
    push %ebx
    push %esi
    push %edi
    mov 20(%esp), %ebx
    mov %esp, 0x820(%ebx)
    sgdt 0x828(%ebx)
    sidt 0x840(%ebx)
    mov %ebx, probe_base
    lgdt 0x830(%ebx)
    ljmp *0x838(%ebx)
.globl boot_e820_probe_return
boot_e820_probe_return:
    mov $16, %eax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %ax, %fs
    mov %ax, %gs
    mov probe_base, %ebx
    lgdt 0x828(%ebx)
    lidt 0x840(%ebx)
    mov 0x820(%ebx), %esp
    mov 0xf00(%ebx), %eax
    pop %edi
    pop %esi
    pop %ebx
    pop %ebp
    ret
.bss
.balign 4
probe_base: .long 0
.section .note.GNU-stack,"",@progbits
