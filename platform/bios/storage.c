/* SPDX-License-Identifier: GPL-3.0-or-later */
/* EDD/CHS provider follows GRUB disk/i386/pc/biosdisk.c.
 * Copyright (C) 1999,2000,2001,2002,2003,2004,2005,2006,2007,2008,2009,2010
 * Free Software Foundation, Inc. New explicit status and bounded bounce API. */
#include "../../core/storage/internal.h"
#include <boot/context.h>
#include <boot/storage_platform.h>
extern const uint8_t boot_disk_thunk_start[], boot_disk_thunk_end[], boot_disk_real_offset[];
extern void boot_disk_call(uint32_t), boot_disk_return(void);
static uint8_t *low;
static int state_failed;
struct regs
{
    uint32_t eax, ebx, ecx, edx, esi, edi, flags;
};
struct device
{
    uint8_t drive, edd;
    uint32_t sector, heads, spt;
    uint64_t blocks;
};
static void put16(uint8_t *p, uint16_t n)
{
    p[0] = n;
    p[1] = n >> 8;
}
static void put32(uint8_t *p, uint32_t n)
{
    for (unsigned i = 0; i < 4; ++i)
        p[i] = n >> (8 * i);
}
static boot_status_t call(struct regs *r)
{
    uint32_t cr0;
    __asm__ volatile("mov %%cr0,%0" : "=r"(cr0));
    if (cr0 & 0x80000000u)
        return BOOT_E_UNSUPPORTED;
    copy(low + 0xa00, r, sizeof(*r));
    boot_disk_call((uintptr_t)low);
    uint32_t after0, after3, after4;
    __asm__ volatile("mov %%cr0,%0; mov %%cr3,%1; mov %%cr4,%2"
                     : "=r"(after0), "=r"(after3), "=r"(after4));
    if (low[0xa1c] != 1 || after0 != u32(low + 0x850) || after3 != u32(low + 0x854) ||
        after4 != u32(low + 0x858))
    {
        state_failed = 1;
        return BOOT_E_IO;
    }
    copy(r, low + 0xa00, sizeof(*r));
    return r->flags & 1 ? BOOT_E_IO : BOOT_OK;
}
static boot_status_t read_blocks(void *opaque, uint64_t lba, uint32_t count, void *buffer)
{
    struct device *d = opaque;
    if (count != 1 || lba >= d->blocks)
        return BOOT_E_INVALID;
    for (unsigned attempt = 0; attempt < 3; ++attempt)
    {
        struct regs r = {.edx = d->drive};
        if (d->edd)
        {
            zero(low + 0xb00, 16);
            low[0xb00] = 16;
            put16(low + 0xb02, 1);
            put16(low + 0xb04, 0x1000);
            put16(low + 0xb06, (uintptr_t)low >> 4);
            for (unsigned i = 0; i < 8; ++i)
                low[0xb08 + i] = (uint8_t)(lba >> (i * 8));
            r.eax = 0x4200;
            r.esi = 0xb00;
        }
        else
        {
            uint32_t track, sector, head;
            uint64_t t = divide(lba, d->spt, &sector);
            track = (uint32_t)divide(t, d->heads, &head);
            if (track >= 1024)
                return BOOT_E_INVALID;
            r.eax = 0x0201;
            r.ebx = 0x1000;
            r.ecx = (track & 255) << 8 | ((track >> 2) & 0xc0) | (sector + 1);
            r.edx |= head << 8;
        }
        if (!call(&r))
        {
            copy(buffer, low + 0x1000, d->sector);
            return BOOT_OK;
        }
        r = (struct regs){.edx = d->drive};
        call(&r);
    }
    return BOOT_E_IO;
}
static boot_status_t prepare(struct boot_context *c)
{
    if (low)
        return BOOT_OK;
    uint64_t base;
    TRY(boot_memory_alloc(&c->memory, BOOT_BL, 65536, 65536, 0x10000, 0x80000, &base));
    low = (uint8_t *)(uintptr_t)base;
    zero(low, 65536);
    copy(low, boot_disk_thunk_start, (size_t)(boot_disk_thunk_end - boot_disk_thunk_start));
    put16(low + 0x810, (uintptr_t)boot_disk_real_offset);
    put16(low + 0x812, (uint32_t)base >> 4);
    put32(low + 0x818, (uintptr_t)boot_disk_return);
    put16(low + 0x81c, 8);
    put16(low + 0x830, 39);
    put32(low + 0x832, (uint32_t)base + 0x900);
    put32(low + 0x838, 0);
    put16(low + 0x83c, 24);
    put16(low + 0x848, 1023);
    uint64_t *gdt = (uint64_t *)(low + 0x900);
    gdt[1] = UINT64_C(0x00cf9a000000ffff);
    gdt[2] = UINT64_C(0x00cf92000000ffff);
    gdt[3] = UINT64_C(0xffff) | (base & 0xffffff) << 16 | UINT64_C(0x9a) << 40 | (base >> 24) << 56;
    gdt[4] = UINT64_C(0xffff) | (base & 0xffffff) << 16 | UINT64_C(0x92) << 40 | (base >> 24) << 56;
    return BOOT_OK;
}
boot_status_t boot_platform_storage_scan(struct boot_context *c, struct boot_storage *s)
{
    static const struct boot_block_ops ops = {read_blocks, NULL};
    TRY(boot_storage_rescan(s));
    state_failed = 0;
    boot_status_t ps = prepare(c);
    if (ps)
    {
        boot_console(c, "disk-thunk-allocation:");
        return ps;
    }
    for (unsigned index = 0; index < 34; ++index)
    {
        unsigned drive = index < 2 ? index : index < 18 ? 0x80 + index - 2 : 0xe0 + index - 18;
        struct device d = {.drive = (uint8_t)drive, .sector = 512};
        struct regs r = {.eax = 0x4100, .ebx = 0x55aa, .edx = drive};
        if (!call(&r) && (r.ebx & 0xffff) == 0xaa55 && (r.ecx & 1))
        {
            zero(low + 0xb00, 74);
            put16(low + 0xb00, 74);
            r = (struct regs){.eax = 0x4800, .edx = drive, .esi = 0xb00};
            if (call(&r) || u16(low + 0xb00) < 26)
                continue;
            d.edd = 1;
            d.blocks = u64(low + 0xb10);
            d.sector = u16(low + 0xb18);
        }
        else
        {
            r = (struct regs){.eax = 0x0800, .edx = drive};
            if (drive >= 0xe0 || call(&r))
                continue;
            d.spt = r.ecx & 63;
            d.heads = ((r.edx >> 8) & 255) + 1;
            uint32_t cylinders = ((r.ecx >> 8) & 255) | ((r.ecx & 0xc0) << 2);
            if (!d.spt)
                continue;
            d.blocks = (uint64_t)(cylinders + 1) * d.heads * d.spt;
        }
        if (!d.blocks || !power2(d.sector) || d.sector < 512 || d.sector > 4096)
            continue;
        /* EDD may return an unknown length for ATAPI, including an empty tray.
         * For ISO media obtain a bounded length from the volume descriptor. */
        if (d.blocks == UINT64_MAX && d.sector == 2048)
        {
            uint8_t descriptor[2048];
            if (read_blocks(&d, 16, 1, descriptor) || !equal(descriptor + 1, "CD001", 5))
                continue;
            d.blocks = u32(descriptor + 80);
        }
        if (!d.blocks || d.blocks > (UINT64_MAX >> shift(d.sector)))
            continue;
        if (s->count == BOOT_DISKS)
            return BOOT_E_NOMEM;
        copy(s->provider_data[s->count], &d, sizeof(d));
        struct boot_block b = {.ops = &ops,
                               .opaque = s->provider_data[s->count],
                               .blocks = d.blocks,
                               .logical_size = d.sector,
                               .physical_size = d.sector,
                               .io_alignment = 1,
                               .max_blocks = 1,
                               .provider = "bios-int13"};
        /* BIOS drive numbers are session aliases, never stable hardware IDs. */
        b.identity[0] = (uint8_t)drive;
        struct boot_slice v;
        boot_status_t added = boot_block_add(s, &b, &v);
        if (added)
            return added;
    }
    if (state_failed)
        return BOOT_E_IO;
    boot_console(c, "BOOT:PASS:storage-bios-cpu-fp\r\n");
    return BOOT_OK;
}
