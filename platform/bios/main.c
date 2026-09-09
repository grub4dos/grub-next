/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <boot/context.h>
#include <boot/fp.h>
#include <boot/physical.h>
extern void boot_bios_platform(struct boot_context *);
extern void boot_core_test(struct boot_context *);
struct boot_efi_services;
extern void boot_module_probe(struct boot_context *, struct boot_efi_services *);
extern boot_status_t boot_bios_resident_install(struct boot_context *);
extern const char boot_image_start[], boot_image_end[];
static struct boot_context context;
static uint8_t pattern[8192], result[8192];
static uint32_t hash(const uint8_t *p, size_t size)
{
    uint32_t h = 2166136261u;
    while (size--)
        h = (h ^ *p++) * 16777619u;
    return h;
}
static void physical_test(struct boot_context *c)
{
    uint64_t high;
    if (boot_memory_alloc(&c->memory, BOOT_BL, UINT64_MAX, 4096, 0, 0x80000000, &high) !=
            BOOT_E_NOMEM ||
        boot_memory_alloc(&c->memory, BOOT_BL, 4096, 4096, 0, 1, &high) != BOOT_E_NOMEM ||
        boot_phys_copy(c, UINT64_MAX - 1, 0, 8) != BOOT_E_INVALID)
        boot_panic(c, "physical-rejection", 0);
    boot_console(c, "BOOT:PASS:overflow-low-memory-rejection\r\n");
    if (!c->pae)
    {
        boot_console(c, "BOOT:PASS:no-pae-low-fallback\r\n");
        if (boot_memory_alloc(&c->memory, BOOT_RESIDENT, 4 * 1024 * 1024, 4096, 0x1000000,
                              0x70000000, &high))
            boot_panic(c, "low-allocation", 0);
    }
    else if (boot_memory_alloc(&c->memory, BOOT_RESIDENT, 4 * 1024 * 1024, 0x200000,
                               UINT64_C(0x100000000), UINT64_C(1) << c->memory.physical_bits,
                               &high))
    {
        boot_console(c, "BOOT:PASS:no-high-ram\r\n");
        return;
    }
    for (size_t i = 0; i < sizeof(pattern); ++i)
        pattern[i] = (uint8_t)(i * 17 + (i >> 8));
    /* Deliberately cross a PAE 2MiB window; source and destination overlap. */
    uint64_t address = high + 0x200000 - 4096;
    uint32_t before_cr0, before_cr3, before_cr4, after_cr0, after_cr3, after_cr4;
    __asm__ volatile("mov %%cr0,%0; mov %%cr3,%1; mov %%cr4,%2"
                     : "=r"(before_cr0), "=r"(before_cr3), "=r"(before_cr4));
    if (boot_phys_write(c, address, pattern, sizeof(pattern)) ||
        boot_phys_copy(c, address + 17, address, sizeof(pattern)) ||
        boot_phys_read(c, result, address + 17, sizeof(result)) ||
        hash(pattern, sizeof(pattern)) != hash(result, sizeof(result)) || boot_fp_check())
        boot_panic(c, "physical-copy-hash-fp", 0);
    __asm__ volatile("mov %%cr0,%0; mov %%cr3,%1; mov %%cr4,%2"
                     : "=r"(after_cr0), "=r"(after_cr3), "=r"(after_cr4));
    if (before_cr0 != after_cr0 || before_cr3 != after_cr3 || before_cr4 != after_cr4)
        boot_panic(c, "paging-state", 0);
    boot_console(c,
                 c->pae ? "BOOT:PASS:high-pae-copy-hash-fp\r\n" : "BOOT:PASS:low-copy-hash-fp\r\n");
}
static void preserve_command_line(struct boot_context *c)
{
    if (c->entry != BOOT_ENTRY_LINUX || !c->command_line)
        return;
    uint64_t address = (uintptr_t)c->command_line;
    size_t capacity = 0;
    for (size_t i = 0; i < c->memory.count; ++i)
    {
        const struct boot_region *r = &c->memory.regions[i];
        if (r->type == BOOT_MEM_FREE && address >= r->base && address - r->base < r->size)
        {
            uint64_t available = r->size - (address - r->base);
            capacity = available < 2048 ? (size_t)available : 2048;
            break;
        }
    }
    const char *text = c->command_line;
    size_t n = 0;
    while (n < capacity && text[n])
        ++n;
    if (n == capacity)
        boot_panic(c, "linux-command-line", 0);
    c->command_line_size = n;
    for (size_t i = 0; i < c->memory.used; ++i)
    {
        const struct boot_allocation *a = &c->memory.allocations[i];
        if (address >= a->base && address - a->base < a->size &&
            n + 1 <= a->size - (address - a->base))
            return;
    }
    if (boot_memory_reserve(&c->memory, address, n + 1, BOOT_BL))
        boot_panic(c, "command-line-reservation", 0);
}
void boot_platform_main(uint32_t magic, uint32_t info)
{
    struct boot_context *c = &context;
    boot_bios_platform(c);
    boot_bios_physical_init(c);
    /* E820 describes the machine, including RAM inaccessible without PAE. */
    unsigned bits = c->memory.physical_bits;
    c->memory.physical_bits = 52;
    boot_status_t s = BOOT_E_INVALID;
    size_t info_size = 4096;
    if (magic == 0x36d76289 && info && !(info & 7))
    {
        info_size = *(const uint32_t *)(uintptr_t)info;
        if (info_size <= 1024 * 1024 && info_size <= UINT32_MAX - info)
            s = boot_context_multiboot2(c, (void *)(uintptr_t)info, info_size);
    }
    else if (magic == 0x53726448 && info && info <= UINT32_MAX - 4096)
        s = boot_context_linux(c, (void *)(uintptr_t)info, 4096);
    if (s)
        boot_panic(c, "bios-context", 0);
    c->memory.physical_bits = bits;
    if (boot_memory_reserve(&c->memory, 0, 0x10000, BOOT_BL) ||
        boot_memory_reserve(&c->memory, (uintptr_t)boot_image_start,
                            (size_t)(boot_image_end - boot_image_start), BOOT_BL) ||
        boot_memory_reserve(&c->memory, info, info_size, BOOT_BL))
        boot_panic(c, "input-reservation", 0);
    boot_console(c, c->entry == BOOT_ENTRY_MULTIBOOT2 ? "BOOT:PASS:context:multiboot2\r\n"
                                                      : "BOOT:PASS:context:linux\r\n");
    if (boot_fp_check())
        boot_panic(c, "fp-state", 0);
    preserve_command_line(c);
    if (boot_bios_bounce_init(c))
        boot_panic(c, "low-bounce-memory", 0);
    boot_core_test(c);
    physical_test(c);
    boot_module_probe(c, NULL);
    if (boot_bios_resident_install(c))
        boot_panic(c, "resident-e820", 0);
    boot_console(c, "BOOT:PASS:resident-int15-e820\r\n");
    boot_panic(c, "phase1-reset", 1);
}
