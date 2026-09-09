/* SPDX-License-Identifier: GPL-3.0-or-later */
/* PAE setup/state switching adapted from wimboot src/paging.c.
 * Copyright (C) 2021 Michael Brown <mbrown@fensystems.co.uk>.
 * New checked ownership, bounded chunk API and non-paging entry contract. */
#include <boot/physical.h>
#define WINDOW UINT32_C(0x80000000)
#define PAGE UINT32_C(0x200000)
static uint64_t pdpt[4] __attribute__((aligned(4096)));
static uint64_t pd[2048] __attribute__((aligned(4096)));
static unsigned char *bounce;
#define BOUNCE_SIZE 4096
static int ready;
static void cpuid(uint32_t leaf, uint32_t *a, uint32_t *d)
{
    uint32_t b, c;
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(b), "=c"(c), "=d"(*d) : "a"(leaf), "c"(0));
}
boot_status_t boot_bios_physical_init(struct boot_context *c)
{
    uint32_t a, d;
    cpuid(1, &a, &d);
    c->pae = !!(d & (1u << 6));
    c->memory.physical_bits = c->pae ? 36 : 32;
    cpuid(0x80000000, &a, &d);
    if (a >= 0x80000008)
    {
        cpuid(0x80000008, &a, &d);
        if ((a & 255) >= 32 && (a & 255) <= 52 && c->pae)
            c->memory.physical_bits = a & 255;
    }
    for (unsigned i = 0; i < 2048; ++i)
        pd[i] = (uint64_t)i * PAGE | 0x83;
    for (unsigned i = 0; i < 4; ++i)
        pdpt[i] = (uintptr_t)&pd[i * 512] | 1;
    ready = 1;
    return BOOT_OK;
}
boot_status_t boot_bios_bounce_init(struct boot_context *c)
{
    uint64_t address;
    boot_status_t status = boot_memory_alloc(&c->memory, BOOT_BL, BOUNCE_SIZE, BOUNCE_SIZE, 0x10000,
                                             0x80000, &address);
    if (status)
        return status;
    bounce = (uint8_t *)(uintptr_t)address;
    return BOOT_OK;
}
struct transfer_state
{
    int pae, write;
    uint32_t cr0, cr3, cr4;
};
static boot_status_t mapped_chunk(void *opaque, uint64_t address, void *buffer, size_t n)
{
    struct transfer_state *s = opaque;
    uint8_t *bytes = buffer;
    volatile uint8_t *mapped;
    if (s->pae)
    {
        pd[WINDOW / PAGE] = (address & ~((uint64_t)PAGE - 1)) | 0x83;
        __asm__ volatile("mov %0,%%cr4; mov %1,%%cr3; mov %2,%%cr0"
                         :
                         : "r"(s->cr4 | 0x20), "r"((uintptr_t)pdpt), "r"(s->cr0 | 0x80000000u)
                         : "memory");
        mapped = (volatile uint8_t *)(WINDOW + (uintptr_t)(address & (PAGE - 1)));
    }
    else
        mapped = (volatile uint8_t *)(uintptr_t)address;
    for (size_t i = 0; i < n; ++i)
    {
        if (s->write)
            mapped[i] = bytes[i];
        else
            bytes[i] = mapped[i];
    }
    if (s->pae)
        __asm__ volatile("mov %0,%%cr0; mov %1,%%cr3; mov %2,%%cr4"
                         :
                         : "r"(s->cr0), "r"(s->cr3), "r"(s->cr4)
                         : "memory");
    return BOOT_OK;
}
static boot_status_t transfer(struct boot_context *c, uint64_t address, void *buffer, size_t size,
                              int write)
{
    uint32_t cr0, cr3, cr4, flags;
    uintptr_t p = (uintptr_t)buffer;
    if (!c || !ready || !buffer || !size || p >= WINDOW || size > WINDOW - p ||
        boot_memory_access(&c->memory, address, size))
        return BOOT_E_INVALID;
    if (!c->pae && (address >= UINT64_C(0x100000000) || size > UINT64_C(0x100000000) - address))
        return BOOT_E_UNSUPPORTED;
    __asm__ volatile("mov %%cr0,%0; mov %%cr3,%1; mov %%cr4,%2; pushfl; popl %3"
                     : "=r"(cr0), "=r"(cr3), "=r"(cr4), "=r"(flags));
    /* Explicit entry contract: flat protected mode, paging off; do not disable
       someone else's mappings and then execute through unknown linear addresses. */
    if (cr0 & 0x80000000u)
        return BOOT_E_UNSUPPORTED;
    __asm__ volatile("cli" ::: "memory");
    struct transfer_state state = {c->pae, write, cr0, cr3, cr4};
    boot_status_t status = boot_phys_chunks(address, buffer, size, mapped_chunk, &state);
    __asm__ volatile("pushl %0; popfl" : : "r"(flags) : "memory", "cc");
    return status;
}
boot_status_t boot_phys_read(struct boot_context *c, void *out, uint64_t p, size_t n)
{
    return transfer(c, p, out, n, 0);
}
boot_status_t boot_phys_write(struct boot_context *c, uint64_t p, const void *in, size_t n)
{
    return transfer(c, p, (void *)in, n, 1);
}
boot_status_t boot_phys_copy(struct boot_context *c, uint64_t dst, uint64_t src, uint64_t n)
{
    if (!c || boot_memory_access(&c->memory, src, n) || boot_memory_access(&c->memory, dst, n))
        return BOOT_E_INVALID;
    if (!c->pae && (src + n > UINT64_C(0x100000000) || dst + n > UINT64_C(0x100000000)))
        return BOOT_E_UNSUPPORTED;
    if (!bounce)
        return BOOT_E_NOMEM;
    int backward = dst > src && dst - src < n;
    uint64_t done = 0;
    while (done < n)
    {
        size_t chunk = n - done > BOUNCE_SIZE ? BOUNCE_SIZE : (size_t)(n - done);
        uint64_t offset = backward ? n - done - chunk : done;
        boot_status_t s = boot_phys_read(c, bounce, src + offset, chunk);
        if (s)
            return s;
        s = boot_phys_write(c, dst + offset, bounce, chunk);
        if (s)
            return s;
        done += chunk;
    }
    return BOOT_OK;
}
