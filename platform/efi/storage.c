/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <boot/archive.h>
#include <boot/efi.h>
#include <boot/storage_platform.h>
struct media
{
    uint32_t id;
    uint8_t removable, present, partition, readonly, caching;
    uint32_t block_size, alignment;
    _Alignas(8) uint64_t last;
    uint64_t lowest_aligned;
    uint32_t logical_per_physical, optimal;
};
struct block_io
{
    uint64_t revision;
    struct media *media;
    void *reset;
    boot_efi_status(BOOT_EFI *read)(void *, uint32_t, uint64_t, uintptr_t, void *);
    void *write, *flush;
};
struct device
{
    struct block_io *io;
    uint32_t id, block_size;
    uint64_t last;
};
static boot_status_t valid(void *opaque)
{
    struct device *d = opaque;
    struct media *m = d->io->media;
    if (!m || !m->present || m->id != d->id || m->block_size != d->block_size || m->last != d->last)
        return BOOT_E_STALE;
    return BOOT_OK;
}
static boot_status_t read_blocks(void *opaque, uint64_t lba, uint32_t n, void *buffer)
{
    struct device *d = opaque;
    boot_status_t status = valid(d);
    if (status)
        return status;
    boot_efi_status s = d->io->read(d->io, d->id, lba, (uintptr_t)n * d->block_size, buffer);
    if (!s)
        return valid(d);
    return (s & ~(UINTPTR_MAX ^ (UINTPTR_MAX >> 1))) == 13 ? BOOT_E_STALE : BOOT_E_IO;
}
boot_status_t boot_platform_storage_scan(struct boot_context *c, struct boot_storage *s)
{
    static const uint8_t block_guid[16] = {0x21, 0x5b, 0x4e, 0x96, 0x59, 0x64, 0xd2, 0x11,
                                           0x8e, 0x39, 0,    0xa0, 0xc9, 0x69, 0x72, 0x3b};
    static const uint8_t path_guid[16] = {0x91, 0x6e, 0x57, 0x09, 0x3f, 0x6d, 0xd2, 0x11,
                                          0x8e, 0x39, 0,    0xa0, 0xc9, 0x69, 0x72, 0x3b};
    static const struct boot_block_ops ops = {read_blocks, valid};
    if (!c || !c->firmware_table)
        return BOOT_E_INVALID;
    boot_status_t status = boot_storage_rescan(s);
    if (status)
        return status;
    struct boot_efi_services *bs = ((struct boot_efi_system *)c->firmware_table)->services;
    uintptr_t count = 0;
    void **handles = NULL;
    boot_efi_status result = bs->locate_handle_buffer(2, block_guid, NULL, &count, &handles);
    if (result)
        return (result & ~(UINTPTR_MAX ^ (UINTPTR_MAX >> 1))) == 14 ? BOOT_OK : BOOT_E_IO;
    if (count > 4096 || (count && !handles))
    {
        if (handles)
            bs->free_pool(handles);
        return BOOT_E_INVALID;
    }
    for (uintptr_t i = 0; i < count; ++i)
    {
        struct block_io *io = NULL;
        if (bs->handle_protocol(handles[i], block_guid, (void **)&io) || !io || !io->media)
            continue;
        struct media *m = io->media;
        if (m->partition || !m->present)
            continue; /* Parse partitions through the common core. */
        if (s->count == BOOT_DISKS)
        {
            status = BOOT_E_NOMEM;
            break;
        }
        struct device *d = (struct device *)s->provider_data[s->count];
        *d = (struct device){io, m->id, m->block_size, m->last};
        if (m->last == UINT64_MAX)
        {
            status = BOOT_E_INVALID;
            break;
        }
        struct boot_block block = {.ops = &ops,
                                   .opaque = d,
                                   .blocks = m->last + 1,
                                   .logical_size = m->block_size,
                                   .physical_size = m->block_size,
                                   .io_alignment = m->alignment ? m->alignment : 1,
                                   .max_blocks = 1,
                                   .provider = "efi-block-io"};
        if (io->revision >= 0x20001 && m->logical_per_physical)
        {
            if (m->logical_per_physical > UINT32_MAX / (m->block_size ? m->block_size : 1))
            {
                status = BOOT_E_INVALID;
                break;
            }
            block.physical_size = m->block_size * m->logical_per_physical;
            block.alignment_offset = (uint32_t)(m->lowest_aligned & (m->logical_per_physical - 1));
        }
        uint8_t *path = NULL;
        if (!bs->handle_protocol(handles[i], path_guid, (void **)&path) && path)
        {
            size_t length = 0;
            while (length + 4 <= 4096)
            {
                uint32_t n = path[length + 2] | (uint32_t)path[length + 3] << 8;
                if (n < 4 || n > 4096 - length)
                    break;
                length += n;
                if (path[length - n] == 127 && path[length - n + 1] == 255)
                {
                    boot_sha256(path, length, block.identity);
                    block.identity_stable = 1;
                    break;
                }
            }
        }
        struct boot_slice slice;
        status = boot_block_add(s, &block, &slice);
        if (status)
            break;
    }
    bs->free_pool(handles);
    if (status)
    {
        boot_status_t ignored = boot_storage_rescan(s);
        (void)ignored;
    }
    return status;
}
