/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <boot/context.h>
void boot_core_test(struct boot_context *c)
{
    if (c->entry == BOOT_ENTRY_EFI)
    {
        unsigned types = 0;
        for (size_t i = 0; i < c->memory.count; ++i)
        {
            if (c->memory.regions[i].type == BOOT_MEM_BS)
                types |= 1;
            if (c->memory.regions[i].type == BOOT_MEM_RT)
                types |= 2;
            if (c->memory.regions[i].type == BOOT_MEM_LOADER)
                types |= 4;
        }
        if (types != 7)
            boot_panic(c, "efi-memory-types", 0);
        boot_console(c, "BOOT:PASS:efi-bs-rt-loader-types\r\n");
    }
    struct boot_time time;
    uint64_t first, second;
    uint64_t maximum = c->entry == BOOT_ENTRY_EFI ? UINT64_C(0x100000000) : UINT64_C(0x80000000);
    size_t before = c->memory.used;
    if (c->platform.time(&time) || time.month < 1 || time.month > 12)
        boot_panic(c, "time", 0);
    boot_console(c, "BOOT:PASS:firmware-time\r\n");
    if (boot_loader_begin(&c->memory) ||
        boot_memory_alloc(&c->memory, BOOT_LOADER, 8192, 4096, 0x100000, maximum, &first) ||
        boot_memory_alloc(&c->memory, BOOT_LOADER, 4096, 4096, 0x100000, maximum, &second) ||
        first == second || boot_loader_abort(&c->memory) || c->memory.used != before)
        boot_panic(c, "loader-abort", 0);
    if (boot_loader_begin(&c->memory) ||
        boot_memory_alloc(&c->memory, BOOT_LOADER, 4096, 4096, 0x100000, maximum, &first) ||
        boot_loader_commit(&c->memory) || c->memory.used != before + 1 ||
        boot_loader_abort(&c->memory) != BOOT_E_INVALID)
        boot_panic(c, "loader-commit", 0);
    boot_console(c, "BOOT:PASS:loader-abort-commit\r\n");
    if (boot_memory_alloc(&c->memory, BOOT_BL, 4096, 4096, 0x100000, maximum, &first) ||
        boot_memory_release(&c->memory, first))
        boot_panic(c, "bl-free", 0);
    if (boot_memory_alloc(&c->memory, BOOT_RESIDENT, 4096, 4096, 0x100000, maximum, &first))
        boot_panic(c, "resident", 0);
    boot_console(c, "BOOT:PASS:memory-owners\r\n");
    char tail[3];
    boot_ring_write("xyz");
    if (boot_ring_read(tail, 3) != 3 || tail[0] != 'x' || tail[2] != 'z')
        boot_panic(c, "ring", 0);
    boot_console(c, "BOOT:PASS:memory-log\r\n");
}
