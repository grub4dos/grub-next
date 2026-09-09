/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Linux real-mode setup entry at file offset 0x268. The common C runtime
   remains the exact same payload used by the protected-mode Linux entry. */
.code16
.text
.globl setup_start
setup_start:
    cli
    cld
    mov %cs, %ax
    sub $0x20, %ax
    mov %ax, %ds
    int $0x12
    cmp $516, %ax
    jb failed
    mov %ds, %ax
    cmp $0x7f00, %ax
    jb 6f
    cmp $0x8100, %ax
    jb failed
6:
    mov $0x8000, %ax
    mov %ax, %es
    xor %si, %si
    xor %di, %di
    mov $2048, %cx
    rep movsw
    movb $0, %es:0x1e8
    mov $0x2d0, %di
    xor %ebx, %ebx
1:
    mov $0xe820, %eax
    mov $0x534d4150, %edx
    mov $20, %ecx
    int $0x15
    jc failed
    cmp $0x534d4150, %eax
    jne failed
    cmp $20, %ecx
    jb failed
    incb %es:0x1e8
    add $20, %di
    test %ebx, %ebx
    jz enter
    cmpb $128, %es:0x1e8
    jb 1b
failed:
    mov $0xe9, %dx
    mov $'!', %al
    outb %al, %dx
2:
    hlt
    jmp 2b
enter:
    mov $setup_marker, %si
    mov $0xe9, %dx
7:
    lodsb
    test %al, %al
    jz 8f
    outb %al, %dx
    jmp 7b
8:
    xor %eax, %eax
    mov %ds, %ax
    shl $4, %eax
    mov %eax, %ebx
    add $setup_gdt, %eax
    mov %eax, setup_gdtr+2
    add $setup32, %ebx
    mov %ebx, setup_pointer
    lgdt setup_gdtr
    mov %cr0, %eax
    or $1, %eax
    mov %eax, %cr0
    ljmpl *setup_pointer
.code32
setup32:
    mov $16, %eax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov $0x80000, %esi
    mov $0x100000, %eax
    jmp *%eax
.balign 8
setup_gdt:
.quad 0, 0x00cf9a000000ffff, 0x00cf92000000ffff
setup_gdtr:
.word 23
.long 0
setup_pointer:
.long 0
.word 8
setup_marker: .asciz "BOOT:PASS:linux-real-setup\n"
.section .note.GNU-stack,"",@progbits
