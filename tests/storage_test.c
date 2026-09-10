/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <boot/storage.h>
#include <stdio.h>
#include <string.h>
static struct boot_storage storage;
static unsigned calls, block_size, media = 1;
static uint8_t disk[65536];
static boot_status_t validate(void *p)
{
    (void)p;
    return media == 1 ? BOOT_OK : BOOT_E_STALE;
}
static boot_status_t read_blocks(void *p, uint64_t lba, uint32_t n, void *buffer)
{
    (void)p;
    assert(!((uintptr_t)buffer & 4095));
    assert(n == 1);
    ++calls;
    if (lba == 15)
        return BOOT_E_IO;
    memcpy(buffer, disk + lba * block_size, n * block_size);
    return BOOT_OK;
}
int main(void)
{
    const struct boot_block_ops ops = {read_blocks, validate};
    for (unsigned bs = 512; bs <= 4096; bs *= 2)
    {
        if (bs == 1024)
            continue;
        block_size = bs;
        media = 1;
        calls = 0;
        for (size_t i = 0; i < sizeof(disk); ++i)
            disk[i] = (uint8_t)(i * 13 + (i >> 8));
        assert(boot_storage_rescan(&storage) == BOOT_OK);
        struct boot_block d = {.ops = &ops,
                               .logical_size = bs,
                               .physical_size = 4096,
                               .io_alignment = 4096,
                               .max_blocks = 1,
                               .blocks = 16,
                               .provider = "test"};
        struct boot_slice root, child;
        assert(boot_block_add(&storage, &d, &root) == BOOT_OK);
        uint8_t buffer[8194];
        assert(boot_block_read(&root, bs - 3, buffer + 1, bs + 9) == BOOT_OK);
        assert(!memcmp(buffer + 1, disk + bs - 3, bs + 9));
        unsigned before = calls;
        assert(boot_block_read(&root, bs - 3, buffer + 1, bs + 9) == BOOT_OK && calls == before);
        assert(boot_filter_slice(&root, bs + 3, bs * 2, &child) == BOOT_OK);
        assert(boot_block_read(&child, 7, buffer, 71) == BOOT_OK &&
               !memcmp(buffer, disk + bs + 10, 71));
        assert(boot_block_read(&child, child.size, buffer, 1) == BOOT_E_INVALID);
        assert(boot_block_read(&child, UINT64_MAX, buffer, 8) == BOOT_E_INVALID);
        media = 2;
        assert(boot_block_read(&root, bs, buffer, 8) == BOOT_E_STALE && calls == before);
        media = 1;
        assert(boot_storage_rescan(&storage) == BOOT_OK);
        assert(boot_block_read(&child, 0, buffer, 1) == BOOT_E_STALE);
        assert(boot_block_add(&storage, &d, &child) == BOOT_OK);
        assert(boot_block_read(&child, bs, buffer, 8) == BOOT_OK && calls == before + 1);
        assert(boot_block_read(&child, 15 * bs, buffer, 1) == BOOT_E_IO);
        assert(boot_block_read(&child, bs, buffer, 8) == BOOT_OK && calls == before + 3);
        d.blocks = UINT64_MAX;
        assert(boot_block_add(&storage, &d, &root) == BOOT_E_INVALID);
    }
    puts("PASS: native blocks, unaligned slices, bounded transfers, cache and stale handles");
    return 0;
}
