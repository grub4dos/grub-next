/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <boot/context.h>
extern const uint8_t boot_resident_start[], boot_resident_end[], boot_resident_real_offset[];
extern void boot_e820_probe_return(void);
extern uint32_t boot_e820_probe(uint32_t);
struct e820
{
    uint64_t base, size;
    uint32_t type, attributes;
} __attribute__((packed));
static struct boot_region exported[BOOT_REGIONS];
static void put16(uint8_t *p, uint16_t v)
{
    p[0] = v;
    p[1] = v >> 8;
}
static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i)
        p[i] = v >> (i * 8);
}
static uint64_t descriptor(uint32_t base, uint8_t access)
{
    return UINT64_C(0xffff) | (uint64_t)(base & 0xffffff) << 16 | (uint64_t)access << 40 |
           (uint64_t)(base >> 24) << 56;
}
boot_status_t boot_bios_resident_install(struct boot_context *c)
{
    if (c->memory.transaction || c->memory.frozen)
        return BOOT_E_INVALID;
    uint64_t base;
    boot_status_t s =
        boot_memory_alloc(&c->memory, BOOT_RESIDENT, 65536, 65536, 0x20000, 0x80000, &base);
    if (s)
        return s;
    size_t count = BOOT_REGIONS;
    s = boot_memory_export(&c->memory, exported, &count);
    if (s)
        return s;
    uint8_t *p = (uint8_t *)(uintptr_t)base;
    for (size_t i = 0; i < 65536; ++i)
        p[i] = 0;
    for (size_t i = 0; i < (size_t)(boot_resident_end - boot_resident_start); ++i)
        p[i] = boot_resident_start[i];
    put32(p + 0x800, (uint32_t)count);
    /* Self-contained real-mode handler: all data references are CS-relative. */
    uint32_t old_vector;
    __asm__ volatile("movl 0x54,%0" : "=r"(old_vector));
    put32(p + 0x804, old_vector);
    struct e820 *table = (struct e820 *)(p + 0x1000);
    for (size_t i = 0; i < count; ++i)
    {
        uint32_t type = 2;
        if (exported[i].type == BOOT_MEM_FREE)
            type = 1;
        else if (exported[i].type == BOOT_MEM_ACPI)
            type = 3;
        else if (exported[i].type == BOOT_MEM_NVS)
            type = 4;
        else if (exported[i].type == BOOT_MEM_BAD)
            type = 5;
        table[i] = (struct e820){exported[i].base, exported[i].size, type, 1};
    }
    put16(p + 0x810, (uintptr_t)boot_resident_real_offset);
    put16(p + 0x812, (uint16_t)(base >> 4));
    put32(p + 0x818, (uintptr_t)boot_e820_probe_return);
    put16(p + 0x81c, 8);
    put16(p + 0x848, 1023);
    put16(p + 0x830, 39);
    put32(p + 0x832, (uint32_t)base + 0x900);
    put32(p + 0x838, 0x100);
    put16(p + 0x83c, 24);
    uint64_t *gdt = (uint64_t *)(p + 0x900);
    gdt[0] = 0;
    gdt[1] = UINT64_C(0x00cf9a000000ffff);
    gdt[2] = UINT64_C(0x00cf92000000ffff);
    gdt[3] = descriptor((uint32_t)base, 0x9a);
    gdt[4] = descriptor((uint32_t)base, 0x92);
    c->memory.frozen = 1;
    __asm__ volatile("movl %0,0x54" : : "r"((uint32_t)(base >> 4) << 16) : "memory");
    /* Probe through real INT 15h, not a call to a C imitation of the handler. */
    if (boot_e820_probe((uint32_t)base) != count)
        return BOOT_E_IO;
    for (size_t i = 0; i < count * sizeof(struct e820); ++i)
        if (p[0x1000 + i] != p[0x4000 + i])
            return BOOT_E_IO;
    return BOOT_OK;
}
