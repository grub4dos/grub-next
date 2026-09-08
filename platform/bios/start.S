/* SPDX-License-Identifier: GPL-3.0-or-later */
.section .multiboot2,"a"
.balign 8
header:
.long 0xe85250d6, 0, header_end-header
.long -(0xe85250d6 + (header_end-header))
.short 0, 0
.long 8
header_end:
.section .text.start,"ax"
.code32
.globl _start
_start:
    cli
    cld
    mov %eax, %esi
    mov %ebx, %edi
    lgdt gdt_ptr
    ljmp $8, $1f
1:
    mov $16, %eax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %ax, %fs
    mov %ax, %gs
    mov $stack_top, %esp
    /* Test EFLAGS.ID before executing CPUID. No C before SSE2 setup. */
    pushfl
    pop %ecx
    mov %ecx, %eax
    xor $0x200000, %eax
    push %eax
    popfl
    pushfl
    pop %eax
    push %ecx
    popfl
    xor %ecx, %eax
    test $0x200000, %eax
    jz unsupported
    xor %eax, %eax
    cpuid
    cmp $1, %eax
    jb unsupported
    mov $1, %eax
    cpuid
    and $0x07000001, %edx /* FPU, FXSR, SSE, SSE2 */
    cmp $0x07000001, %edx
    jne unsupported
    mov %cr0, %eax
    and $~12, %eax /* clear EM and TS: eager FP */
    or $0x22, %eax /* MP and NE */
    mov %eax, %cr0
    mov %cr4, %eax
    or $0x600, %eax /* OSFXSR and OSXMMEXCPT */
    mov %eax, %cr4
    fninit
    ldmxcsr mxcsr
    sub $8, %esp
    push %edi
    push %esi
    call boot_bios_main
halt:
    cli
    hlt
    jmp halt
unsupported:
    /* Integer-only failure path, including CPUs without CPUID. */
    mov $failure, %esi
    mov $0xe9, %dx
2:
    lodsb
    test %al, %al
    jz halt
    outb %al, %dx
    jmp 2b
.section .rodata,"a"
.balign 8
gdt:
.quad 0, 0x00cf9a000000ffff, 0x00cf92000000ffff
gdt_ptr:
.word 23
.long gdt
mxcsr: .long 0x1f80
failure: .asciz "BOOT:FAIL:cpu-sse2-required\n"
.section .bss,"aw",@nobits
.balign 16
stack: .skip 16384
stack_top:
.section .note.GNU-stack,"",@progbits
