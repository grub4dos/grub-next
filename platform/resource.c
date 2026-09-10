/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "resource_image.h"
#include <boot/resource.h>
#if BOOT_MODULE_TARGET_ID == 1
#include <boot/physical.h>
#endif
boot_status_t boot_resource_open(struct boot_context *c, struct boot_archive *a)
{
    if (!c || !a)
        return BOOT_E_INVALID;
    a->count = 0;
    const void *data = boot_resource_image;
    size_t size = sizeof(boot_resource_image);
    const char *marker = "BOOT:PASS:resource-embedded\r\n";
#if BOOT_MODULE_TARGET_ID == 1
    uint64_t copy = 0;
    uint64_t input_base = c->resource_base, input_size = c->resource_size;
    if (c->entry == BOOT_ENTRY_MULTIBOOT2 && c->module_count)
    {
        input_base = c->modules[0].base;
        input_size = c->modules[0].size;
    }
    if (input_base || input_size || c->module_count)
    {
        if (!input_size || input_size > BOOT_ARCHIVE_MAX_SIZE)
            return BOOT_E_INVALID;
        size = (size_t)input_size;
        boot_status_t s =
            boot_memory_alloc(&c->memory, BOOT_BL, size, 4096, 0x1000000, 0x80000000, &copy);
        if (s)
            return s;
        data = (void *)(uintptr_t)copy;
        s = boot_phys_read(c, (void *)data, input_base, size);
        if (s)
        {
            boot_memory_release(&c->memory, copy);
            return s;
        }
        marker = c->entry == BOOT_ENTRY_MULTIBOOT2 ? "BOOT:PASS:resource-multiboot2\r\n"
                                                   : "BOOT:PASS:resource-linux-initrd\r\n";
    }
#endif
    boot_status_t s = boot_archive_open(a, data, size);
    if (!s)
        s = boot_archive_verify(a);
    if (s)
    {
        a->count = 0;
#if BOOT_MODULE_TARGET_ID == 1
        if (copy)
            boot_memory_release(&c->memory, copy);
#endif
        return s;
    }
    /* The BL copy (or image section) lives through resource consumers. */
    boot_console(c, marker);
    boot_console(c, "BOOT:PASS:resource-manifest\r\n");
    return BOOT_OK;
}
