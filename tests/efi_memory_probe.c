/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <boot/efi.h>
void boot_efi_memory_test(struct boot_context *c)
{
    static uint8_t data[65536] __attribute__((aligned(8)));
    struct boot_efi_system *s = c->firmware_table;
    uintptr_t size = sizeof(data), key, stride;
    uint32_t version;
    if (s->services->get_memory_map(&size, data, &key, &stride, &version) ||
        stride < sizeof(struct boot_efi_descriptor) || size % stride)
        boot_panic(c, "efi-allocation-map", 0);
    for (size_t a = 0; a < c->memory.used; ++a)
    {
        const struct boot_allocation *allocation = &c->memory.allocations[a];
        uint64_t cursor = allocation->base, end = cursor + allocation->size;
        uint32_t expected = allocation->owner == BOOT_RESIDENT ? 0 : 2;
        while (cursor < end)
        {
            int found = 0;
            for (size_t off = 0; off < size; off += stride)
            {
                const struct boot_efi_descriptor *d = (const void *)(data + off);
                if (cursor < d->physical || cursor - d->physical >= d->pages * 4096)
                    continue;
                if (d->type != expected)
                    boot_panic(c, "efi-allocation-type", 0);
                cursor = d->physical + d->pages * 4096;
                found = 1;
                break;
            }
            if (!found)
                boot_panic(c, "efi-allocation-missing", 0);
        }
    }
    boot_console(c, "BOOT:PASS:efi-firmware-reservations\r\n");
}
