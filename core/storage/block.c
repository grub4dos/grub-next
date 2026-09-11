/* SPDX-License-Identifier: GPL-3.0-or-later */
/* GRUB disk.c/disk_common.c byte adapter and cache design, reworked for native
 * blocks and explicit status/ownership. Copyright (C) 2002,2003,2004,2006,2007,
 * 2008,2009,2010 Free Software Foundation, Inc. */
#include "internal.h"
boot_status_t boot_storage_rescan(struct boot_storage *s)
{
    if (!s || s->busy || s->generation == UINT64_MAX)
        return BOOT_E_INVALID;
    ++s->generation;
    s->count = 0;
    for (unsigned i = 0; i < BOOT_CACHE_LINES; ++i)
        s->cache[i].valid = 0;
    return BOOT_OK;
}
boot_status_t boot_block_add(struct boot_storage *s, const struct boot_block *d,
                             struct boot_slice *out)
{
    if (!s || !d || !out || !s->generation || s->busy || !d->ops || !d->ops->read ||
        !power2(d->logical_size) || d->logical_size < 512 || d->logical_size > BOOT_BLOCK_MAX ||
        !power2(d->physical_size) || d->physical_size < d->logical_size ||
        !power2(d->io_alignment) || d->io_alignment > 4096 || !d->max_blocks ||
        d->alignment_offset >= d->physical_size / d->logical_size || !d->blocks ||
        d->blocks > (UINT64_MAX >> shift(d->logical_size)))
        return BOOT_E_INVALID;
    if (s->count == BOOT_DISKS)
        return BOOT_E_NOMEM;
    struct boot_block *b = &s->disks[s->count];
    *b = *d;
    b->slot = (uint32_t)s->count++;
    b->storage = s;
    b->generation = s->generation;
    zero(out, sizeof(*out));
    out->storage = s;
    out->generation = s->generation;
    out->slot = b->slot;
    out->size = b->blocks << shift(b->logical_size);
    return BOOT_OK;
}
boot_status_t boot_slice_validate(const struct boot_slice *v)
{
    if (!v || !v->storage)
        return BOOT_E_INVALID;
    struct boot_storage *s = v->storage;
    if (v->generation != s->generation || v->slot >= s->count)
        return BOOT_E_STALE;
    struct boot_block *b = &s->disks[v->slot];
    if (b->generation != v->generation)
        return BOOT_E_STALE;
    if (!range(v->offset, v->size, b->blocks << shift(b->logical_size)))
        return BOOT_E_INVALID;
    boot_status_t status = b->ops->validate ? b->ops->validate(b->opaque) : BOOT_OK;
    if (status)
        for (unsigned i = 0; i < BOOT_CACHE_LINES; ++i)
            s->cache[i].valid = 0;
    return status;
}
boot_status_t boot_filter_slice(const struct boot_slice *p, uint64_t off, uint64_t n,
                                struct boot_slice *v)
{
    if (!v)
        return BOOT_E_INVALID;
    TRY(boot_slice_validate(p));
    if (!n || !range(off, n, p->size) || p->depth >= 16)
        return BOOT_E_INVALID;
    *v = *p;
    v->offset += off;
    v->size = n;
    ++v->depth;
    return BOOT_OK;
}
boot_status_t boot_block_read(const struct boot_slice *v, uint64_t off, void *buf, size_t n)
{
    if (!v || (!buf && n) || !range(off, n, v->size))
        return BOOT_E_INVALID;
    TRY(boot_slice_validate(v));
    struct boot_storage *s = v->storage;
    uint32_t bit = UINT32_C(1) << v->slot;
    if (s->busy & bit)
        return BOOT_E_INVALID;
    struct boot_block *b = &s->disks[v->slot];
    uint8_t *p = buf;
    unsigned bits = shift(b->logical_size);
    off += v->offset;
    s->busy |= bit;
    boot_status_t status = BOOT_OK;
    while (n)
    {
        uint64_t lba = off >> bits;
        size_t intra = (size_t)off & (b->logical_size - 1), take = b->logical_size - intra;
        if (take > n)
            take = n;
        struct boot_cache_line *c = &s->cache[(lba ^ v->slot) & (BOOT_CACHE_LINES - 1)];
        if (!c->valid || c->generation != v->generation || c->slot != v->slot || c->lba != lba)
        {
            c->valid = 0;
            /* A parent read can collide with this cache line. Publish only
             * after the nested read has finished, from an aligned scratch. */
            _Alignas(4096) uint8_t scratch[BOOT_BLOCK_MAX];
            status = b->ops->read(b->opaque, lba, 1, scratch);
            if (status)
                break;
            copy(c->data, scratch, b->logical_size);
            c->generation = v->generation;
            c->slot = v->slot;
            c->lba = lba;
            c->valid = 1;
        }
        copy(p, c->data + intra, take);
        p += take;
        off += take;
        n -= take;
    }
    if (status)
        for (unsigned i = 0; i < BOOT_CACHE_LINES; ++i)
            s->cache[i].valid = 0;
    s->busy &= ~bit;
    return status;
}
