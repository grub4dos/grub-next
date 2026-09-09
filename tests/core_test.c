/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <boot/efi.h>
#include <boot/physical.h>
#include <stdio.h>
#include <string.h>
static void put32(uint8_t *p, uint32_t n)
{
    for (unsigned i = 0; i < 4; ++i)
        p[i] = n >> (8 * i);
}
static void put64(uint8_t *p, uint64_t n)
{
    put32(p, (uint32_t)n);
    put32(p + 4, (uint32_t)(n >> 32));
}
static void memory_test(void)
{
    struct boot_memory m = {.physical_bits = 40};
    uint64_t p, q;
    assert(!boot_memory_add(&m, 0x10000, UINT64_C(0x200000000), BOOT_MEM_FREE));
    assert(boot_memory_add(&m, 0, 0x20000, BOOT_MEM_FREE) == BOOT_E_INVALID);
    assert(boot_memory_add(&m, UINT64_MAX - 1, 8, BOOT_MEM_FREE) == BOOT_E_INVALID);
    assert(boot_memory_alloc(&m, BOOT_LOADER, 1, 1, 0, 0x100000, &p) == BOOT_E_INVALID);
    assert(!boot_loader_begin(&m));
    assert(boot_loader_begin(&m) == BOOT_E_INVALID);
    assert(!boot_memory_alloc(&m, BOOT_LOADER, UINT64_C(0x100001000), 4096, 0x10000,
                              UINT64_C(0x200010000), &p));
    assert(!boot_memory_access(&m, p + UINT64_C(0x100000000), 4096));
    assert(!boot_loader_abort(&m) && !m.used);
    assert(!boot_memory_alloc(&m, BOOT_RESIDENT, 8192, 4096, UINT64_C(0xfffff000),
                              UINT64_C(0x100001000), &p));
    assert(p == UINT64_C(0xfffff000));
    assert(!boot_memory_access(&m, UINT64_C(0x100000000), 4096));
    assert(boot_memory_access(&m, p + 8191, 2) == BOOT_E_INVALID);
    assert(boot_memory_access(&m, UINT64_MAX - 1, 8) == BOOT_E_INVALID);
    assert(boot_memory_release(&m, p) == BOOT_E_INVALID);
    assert(!boot_loader_begin(&m));
    assert(!boot_memory_alloc(&m, BOOT_LOADER, 8192, 4096, 0x10000, 0x100000, &q));
    assert(!boot_loader_commit(&m));
    assert(boot_loader_abort(&m) == BOOT_E_INVALID);
    assert(!boot_loader_begin(&m));
    assert(!boot_loader_abort(&m) && m.used == 2);
    struct boot_region regions[8];
    size_t count = 0;
    assert(boot_memory_export(&m, 0, &count) == BOOT_E_NOMEM && count == 5);
    count = 8;
    assert(!boot_memory_export(&m, regions, &count));
    uint64_t size = 0;
    for (size_t i = 0; i < count; ++i)
    {
        size += regions[i].size;
        if (i)
            assert(regions[i - 1].base + regions[i - 1].size == regions[i].base);
        if (regions[i].base == p)
            assert(regions[i].type == BOOT_MEM_RESERVED);
    }
    assert(size == UINT64_C(0x200000000));
    assert(boot_memory_alloc(&m, BOOT_BL, 1, 3, 0, 100, &q) == BOOT_E_INVALID);
    assert(boot_memory_alloc(&m, BOOT_BL, 4096, 4096, 0, 1, &q) == BOOT_E_NOMEM);
    assert(boot_memory_alloc(&m, BOOT_BL, UINT64_MAX, 4096, 0, 0x100000, &q) == BOOT_E_NOMEM);
    /* Realistic PCI hole: crossing 4GiB must fail, not consume reserved space. */
    struct boot_memory hole = {.physical_bits = 40};
    assert(!boot_memory_add(&hole, 0xe0000000, 0x20000000, BOOT_MEM_RESERVED));
    assert(!boot_memory_add(&hole, UINT64_C(0x100000000), 0x40000000, BOOT_MEM_FREE));
    assert(boot_memory_alloc(&hole, BOOT_RESIDENT, 8192, 4096, UINT64_C(0xfffff000),
                             UINT64_C(0x100001000), &q) == BOOT_E_NOMEM);
    /* Fragmentation, alignment, free, and transactional reuse. */
    uint32_t random = 7;
    for (unsigned round = 0; round < 100; ++round)
    {
        struct boot_memory f = {.physical_bits = 36};
        assert(!boot_memory_add(&f, 0x10000, 0x100000, BOOT_MEM_FREE));
        assert(!boot_loader_begin(&f));
        for (unsigned j = 0; j < 80; ++j)
        {
            random = random * 1664525u + 1013904223u;
            uint64_t n = (random & 4095) + 1;
            assert(!boot_memory_alloc(&f, j & 1 ? BOOT_BL : BOOT_LOADER, n, 16, 0x10000, 0x110000,
                                      &q));
            assert(!(q & 15));
            for (size_t a = 0; a + 1 < f.used; ++a)
                assert(q + n <= f.allocations[a].base ||
                       q >= f.allocations[a].base + f.allocations[a].size);
        }
        assert(!boot_loader_abort(&f) && f.used == 40);
        while (f.used)
            assert(!boot_memory_release(&f, f.allocations[0].base));
    }
}
static void context_test(void)
{
    uint8_t mb[56] = {0};
    struct boot_context c = {.memory.physical_bits = 40};
    put32(mb, 56);
    put32(mb + 8, 6);
    put32(mb + 12, 40);
    put32(mb + 16, 24);
    put64(mb + 24, UINT64_C(0x100000000));
    put64(mb + 32, 0x200000);
    put32(mb + 40, 1);
    put32(mb + 52, 8);
    assert(!boot_context_multiboot2(&c, mb, sizeof(mb)));
    assert(c.entry == BOOT_ENTRY_MULTIBOOT2 && c.memory.regions[0].base == UINT64_C(0x100000000));
    for (size_t n = 0; n < sizeof(mb); ++n)
    {
        memset(&c, 0, sizeof(c));
        c.memory.physical_bits = 40;
        assert(boot_context_multiboot2(&c, mb, n) == BOOT_E_INVALID);
    }
    put32(mb + 16, 0);
    assert(boot_context_multiboot2(&c, mb, sizeof(mb)) == BOOT_E_INVALID);
    uint8_t metadata[136] = {0};
    put32(metadata, sizeof(metadata));
    put32(metadata + 8, 6);
    put32(metadata + 12, 40);
    put32(metadata + 16, 24);
    put64(metadata + 24, 0x100000);
    put64(metadata + 32, 0x400000);
    put32(metadata + 40, 1);
    put32(metadata + 48, 1);
    put32(metadata + 52, 12);
    memcpy(metadata + 56, "abc", 4);
    put32(metadata + 64, 8);
    put32(metadata + 68, 40);
    put64(metadata + 72, UINT64_C(0x100000000));
    put32(metadata + 80, 3200);
    put32(metadata + 84, 800);
    put32(metadata + 88, 600);
    metadata[92] = 32;
    metadata[93] = 1;
    metadata[96] = 16;
    metadata[97] = 8;
    put32(metadata + 104, 3);
    put32(metadata + 108, 20);
    put32(metadata + 112, 0x200000);
    put32(metadata + 116, 0x201000);
    memcpy(metadata + 120, "mod", 4);
    put32(metadata + 132, 8);
    memset(&c, 0, sizeof(c));
    c.memory.physical_bits = 40;
    assert(!boot_context_multiboot2(&c, metadata, sizeof(metadata)));
    assert(c.command_line_size == 3 && !memcmp(c.command_line, "abc", 3));
    assert(c.module_count == 1 && c.modules[0].base == 0x200000);
    assert(c.framebuffer.address == UINT64_C(0x100000000) && c.framebuffer.size == 1920000);
    uint8_t linux_info[4096] = {0};
    put32(linux_info + 0x202, 0x53726448);
    linux_info[0x206] = 10;
    linux_info[0x207] = 2;
    linux_info[0x1e8] = 1;
    put64(linux_info + 0x2d0, 0x100000);
    put64(linux_info + 0x2d8, 0x100000);
    put32(linux_info + 0x2e0, 1);
    memset(&c, 0, sizeof(c));
    c.memory.physical_bits = 40;
    assert(!boot_context_linux(&c, linux_info, sizeof(linux_info)));
    assert(c.entry == BOOT_ENTRY_LINUX);
    linux_info[0x1e8] = 129;
    assert(boot_context_linux(&c, linux_info, sizeof(linux_info)) == BOOT_E_INVALID);
    linux_info[0x1e8] = 1;
    put32(linux_info + 0x250, 1);
    assert(boot_context_linux(&c, linux_info, sizeof(linux_info)) == BOOT_E_UNSUPPORTED);
    struct
    {
        struct boot_efi_descriptor d;
        uint64_t extension;
    } descriptors[4] = {{{3, 0, 0x1000, 0, 1, 0}, 0},
                        {{5, 0, 0x2000, 0, 1, 0}, 0},
                        {{1, 0, 0x3000, 0, 1, 0}, 0},
                        {{7, 0, UINT64_C(0x100000000), 0, 1, 0}, 0}};
    struct boot_memory m = {.physical_bits = 40};
    assert(!boot_efi_import_map(&m, descriptors, sizeof(descriptors), sizeof(descriptors[0])));
    assert(m.regions[0].type == BOOT_MEM_BS && m.regions[1].type == BOOT_MEM_RT &&
           m.regions[2].type == BOOT_MEM_LOADER && m.regions[3].type == BOOT_MEM_FREE);
    assert(boot_efi_import_map(&m, descriptors, 1, 48) == BOOT_E_INVALID);
    assert(boot_efi_import_map(&m, descriptors, 48, 1) == BOOT_E_INVALID);
    descriptors[0].d.pages = UINT64_MAX;
    memset(&m, 0, sizeof(m));
    m.physical_bits = 40;
    assert(boot_efi_import_map(&m, descriptors, sizeof(descriptors), 48) == BOOT_E_INVALID);
    _Static_assert(offsetof(struct boot_efi_services, load_image) == 24 + 22 * sizeof(void *),
                   "LoadImage");
    _Static_assert(offsetof(struct boot_efi_services, locate_protocol) == 24 + 37 * sizeof(void *),
                   "LocateProtocol");
    _Static_assert(offsetof(struct boot_efi_runtime, reset) == 24 + 10 * sizeof(void *),
                   "ResetSystem");
}
struct aperture
{
    uint8_t memory[16384];
    uint64_t base;
    int write;
    unsigned calls;
};
static boot_status_t aperture_chunk(void *opaque, uint64_t address, void *data, size_t n)
{
    struct aperture *a = opaque;
    assert((address & 0x1fffff) + n <= 0x200000);
    assert(address >= a->base && address + n <= a->base + sizeof(a->memory));
    if (a->write)
        memcpy(a->memory + (size_t)(address - a->base), data, n);
    else
        memcpy(data, a->memory + (size_t)(address - a->base), n);
    ++a->calls;
    return BOOT_OK;
}
static void physical_test(void)
{
    struct aperture a = {.base = UINT64_C(0xfffff000), .write = 1};
    uint8_t pattern[16384], result[16384];
    for (size_t i = 0; i < sizeof(pattern); ++i)
        pattern[i] = (uint8_t)(i * 17 + (i >> 8));
    assert(!boot_phys_chunks(a.base, pattern, sizeof(pattern), aperture_chunk, &a));
    assert(a.calls == 2);
    a.write = 0;
    a.calls = 0;
    assert(!boot_phys_chunks(a.base, result, sizeof(result), aperture_chunk, &a));
    assert(a.calls == 2 && !memcmp(pattern, result, sizeof(pattern)));
    assert(boot_phys_chunks(UINT64_MAX - 1, result, 8, aperture_chunk, &a) == BOOT_E_INVALID);
    assert(a.calls == 2);
}
static unsigned reserved_pages, released_pages, reject_pages;
static boot_status_t reserve_page(uint64_t base, uint64_t size, enum boot_owner owner)
{
    assert(!(base & 4095) && !(size & 4095));
    assert(owner == BOOT_BL || owner == BOOT_LOADER || owner == BOOT_RESIDENT);
    ++reserved_pages;
    if (reject_pages)
    {
        --reject_pages;
        return BOOT_E_NOMEM;
    }
    return BOOT_OK;
}
static boot_status_t release_page(uint64_t base, uint64_t size)
{
    assert(!(base & 4095) && !(size & 4095));
    ++released_pages;
    return BOOT_OK;
}
static void firmware_allocator_test(void)
{
    struct boot_memory m = {.physical_bits = 40,
                            .page_size = 4096,
                            .reserve_pages = reserve_page,
                            .free_pages = release_page};
    uint64_t p;
    assert(!boot_memory_add(&m, 0x100000, 0x100000, BOOT_MEM_FREE));
    assert(!boot_memory_alloc(&m, BOOT_BL, 1, 1, 0x100000, 0x200000, &p));
    assert(m.allocations[0].size == 4096);
    assert(!boot_memory_release(&m, p));
    assert(!boot_loader_begin(&m));
    assert(!boot_memory_alloc(&m, BOOT_LOADER, 1, 1, 0x100000, 0x200000, &p));
    assert(!boot_loader_abort(&m));
    assert(reserved_pages == 2 && released_pages == 2);
    reject_pages = 1;
    assert(!boot_memory_alloc(&m, BOOT_BL, 1, 1, 0x100000, 0x200000, &p));
    assert(p == 0x1fe000 && !reject_pages);
    assert(!boot_memory_release(&m, p));
    m.resident_page_size = 65536;
    assert(!boot_memory_alloc(&m, BOOT_RESIDENT, 1, 1, 0x100000, 0x200000, &p));
    assert(!(p & 65535) && m.allocations[0].size == 65536);
    m.frozen = 1;
    assert(boot_memory_alloc(&m, BOOT_BL, 1, 1, 0x100000, 0x200000, &p) == BOOT_E_INVALID);
    assert(boot_loader_begin(&m) == BOOT_E_INVALID);
}
int main(void)
{
    memory_test();
    physical_test();
    firmware_allocator_test();
    context_test();
    char data[5000], out[4096];
    for (unsigned i = 0; i < 4999; ++i)
        data[i] = (char)('a' + i % 26);
    data[4999] = 0;
    boot_ring_write(data);
    assert(boot_ring_read(out, sizeof(out)) == 4096);
    assert(!memcmp(out, data + 4999 - 4096, 4096));
    puts("PASS: memory, 64-bit/cross-4GiB regions, transactions, contexts, EFI types, ring");
    return 0;
}
