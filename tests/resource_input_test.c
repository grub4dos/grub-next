/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "resource_image.h"
#include <assert.h>
#include <boot/resource.h>
#include <stdio.h>
#include <string.h>
static struct boot_archive archive;
static uint8_t copy[sizeof(boot_resource_image)];
static unsigned allocations, releases, reads;
static boot_status_t alloc_error, read_error;
static int corrupt;
boot_status_t boot_memory_alloc(struct boot_memory *m, enum boot_owner owner, uint64_t size,
                                uint64_t alignment, uint64_t minimum, uint64_t maximum,
                                uint64_t *out)
{
    (void)m;
    assert(owner == BOOT_BL && size == sizeof(copy) && alignment == 4096);
    assert(minimum == 0x1000000 && maximum == 0x80000000);
    if (alloc_error)
        return alloc_error;
    ++allocations;
    *out = (uintptr_t)copy;
    return BOOT_OK;
}
boot_status_t boot_memory_release(struct boot_memory *m, uint64_t address)
{
    (void)m;
    assert(address == (uintptr_t)copy);
    ++releases;
    return BOOT_OK;
}
boot_status_t boot_phys_read(struct boot_context *c, void *out, uint64_t address, size_t size)
{
    (void)c;
    assert(address == 0x300000 && size == sizeof(copy));
    ++reads;
    if (read_error)
        return read_error;
    memcpy(out, boot_resource_image, size);
    if (corrupt)
        copy[120] ^= 1;
    return BOOT_OK;
}
boot_status_t boot_console(struct boot_context *c, const char *text)
{
    (void)c;
    (void)text;
    return BOOT_OK;
}
int main(void)
{
    struct boot_context c = {.entry = BOOT_ENTRY_LINUX};
    assert(!boot_resource_open(&c, &archive) && !allocations && !reads);
    c.resource_base = 0x300000;
    assert(boot_resource_open(&c, &archive) == BOOT_E_INVALID && !archive.count);
    c.resource_size = BOOT_ARCHIVE_MAX_SIZE + 1;
    assert(boot_resource_open(&c, &archive) == BOOT_E_INVALID && !allocations);
    c.resource_size = sizeof(copy);
    alloc_error = BOOT_E_NOMEM;
    assert(boot_resource_open(&c, &archive) == BOOT_E_NOMEM && !reads && !archive.count);
    alloc_error = BOOT_OK;
    read_error = BOOT_E_IO;
    assert(boot_resource_open(&c, &archive) == BOOT_E_IO && allocations == releases);
    read_error = BOOT_OK;
    corrupt = 1;
    assert(boot_resource_open(&c, &archive) == BOOT_E_INVALID && !archive.count);
    assert(allocations == releases);
    corrupt = 0;
    assert(!boot_resource_open(&c, &archive) && allocations == releases + 1);
    boot_memory_release(&c.memory, (uintptr_t)copy);
    c.entry = BOOT_ENTRY_MULTIBOOT2;
    c.module_count = 2;
    c.modules[0] = (struct boot_module_input){0x300000, 0, "empty"};
    c.modules[1] = (struct boot_module_input){0x300000, sizeof(copy), "second"};
    assert(boot_resource_open(&c, &archive) == BOOT_E_INVALID && !archive.count);
    c.modules[0].size = sizeof(copy);
    c.resource_base = UINT64_MAX; /* First module wins over the cached input. */
    assert(!boot_resource_open(&c, &archive));
    boot_memory_release(&c.memory, (uintptr_t)copy);
    assert(allocations == releases);
    puts("PASS: resource selection, empty/oversized input, OOM, I/O and hash failure cleanup");
    return 0;
}
