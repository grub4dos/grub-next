/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Adapted from GRUB partmap/gpt.c and msdos.c.
 * Copyright (C) 2002,2004,2005,2006,2007,2008,2009 Free Software Foundation, Inc.
 * Native LBAs, bounded EBR traversal, atomic results and GPT CRCs replace the
 * old global-error/512-sector interfaces. */
#include "internal.h"
static uint32_t crc(uint32_t c, const uint8_t *p, size_t n)
{
    while (n--)
    {
        c ^= *p++;
        for (unsigned j = 0; j < 8; ++j)
            c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
    }
    return c;
}
static boot_status_t add(const struct boot_slice *d, struct boot_slice *out, size_t *n,
                         uint64_t start, uint64_t count, uint32_t number, const uint8_t *id)
{
    unsigned bits = shift(d->storage->disks[d->slot].logical_size);
    if (!start || !count || start > (UINT64_MAX >> bits) || count > (UINT64_MAX >> bits))
        return BOOT_E_CORRUPT;
    if (*n == BOOT_PARTITIONS)
        return BOOT_E_NOMEM;
    uint64_t off = start << bits, size = count << bits;
    if (!range(off, size, d->size))
        return BOOT_E_CORRUPT;
    for (size_t i = 0; i < *n; ++i)
        if (off < out[i].offset - d->offset + out[i].size && out[i].offset - d->offset < off + size)
            return BOOT_E_CORRUPT;
    TRY(boot_filter_slice(d, off, size, &out[*n]));
    out[*n].number = number;
    if (id)
        copy(out[*n].identity, id, 16);
    ++*n;
    return BOOT_OK;
}
static boot_status_t gpt(const struct boot_slice *d, struct boot_slice *out, size_t *n)
{
    uint8_t h[4096], e[128], chunk[512];
    uint32_t bs = d->storage->disks[d->slot].logical_size;
    TRY(boot_block_read(d, bs, h, bs));
    uint32_t len = u32(h + 12), expected = u32(h + 16);
    if (!equal(h, "EFI PART", 8) || u32(h + 8) != 0x10000 || len < 92 || len > bs || u32(h + 20))
        return BOOT_E_CORRUPT;
    zero(h + 16, 4);
    if (~crc(~0u, h, len) != expected || u64(h + 24) != 1 ||
        u64(h + 32) != (d->size >> shift(bs)) - 1)
        return BOOT_E_CORRUPT;
    uint64_t first = u64(h + 40), last = u64(h + 48), table = u64(h + 72);
    uint32_t entries = u32(h + 80), stride = u32(h + 84);
    if (first > last || last >= u64(h + 32) || !entries || entries > BOOT_PARTITIONS ||
        stride < 128 || stride > 4096 || (stride & 127) || table < 2 || table >= first ||
        (uint64_t)entries * stride > (first - table) * bs)
        return BOOT_E_CORRUPT;
    uint32_t c = ~0u;
    for (uint64_t off = 0, total = (uint64_t)entries * stride; off < total;)
    {
        size_t take = total - off > sizeof(chunk) ? sizeof(chunk) : (size_t)(total - off);
        TRY(boot_block_read(d, (table << shift(bs)) + off, chunk, take));
        c = crc(c, chunk, take);
        off += take;
    }
    if (~c != u32(h + 88))
        return BOOT_E_CORRUPT;
    const uint8_t empty[16] = {0};
    for (uint32_t i = 0; i < entries; ++i)
    {
        TRY(boot_block_read(d, (table << shift(bs)) + (uint64_t)i * stride, e, sizeof(e)));
        if (equal(e, empty, 16))
            continue;
        uint64_t a = u64(e + 32), b = u64(e + 40);
        if (a < first || b > last || a > b || equal(e + 16, empty, 16))
            return BOOT_E_CORRUPT;
        for (size_t j = 0; j < *n; ++j)
            if (equal(out[j].identity, e + 16, 16))
                return BOOT_E_CORRUPT;
        TRY(add(d, out, n, a, b - a + 1, i + 1, e + 16));
        copy(out[*n - 1].disk_identity, h + 56, 16);
    }
    return BOOT_OK;
}
static int extended(uint8_t type)
{
    return type == 5 || type == 15 || type == 0x85;
}
static boot_status_t mbr(const struct boot_slice *d, struct boot_slice *out, size_t *n, uint8_t *m)
{
    uint64_t ext = 0, extlen = 0;
    uint8_t diskid[16] = {0};
    copy(diskid, m + 440, 4);
    for (unsigned i = 0; i < 4; ++i)
    {
        uint8_t *e = m + 446 + 16 * i;
        if (e[0] & 127)
            return BOOT_E_CORRUPT;
        if (!e[4])
            continue;
        uint64_t a = u32(e + 8), b = u32(e + 12);
        if (extended(e[4]))
        {
            if (ext || !a || !b ||
                !range(a, b, d->size >> shift(d->storage->disks[d->slot].logical_size)))
                return BOOT_E_CORRUPT;
            ext = a;
            extlen = b;
        }
        else
            TRY(add(d, out, n, a, b, i + 1, NULL));
    }
    uint64_t pos = ext, seen[BOOT_PARTITIONS];
    size_t visited = 0;
    while (pos)
    {
        if (visited == BOOT_PARTITIONS || !range(pos, 1, ext + extlen) || pos < ext)
            return BOOT_E_CORRUPT;
        for (size_t i = 0; i < visited; ++i)
            if (seen[i] == pos)
                return BOOT_E_CORRUPT;
        seen[visited++] = pos;
        TRY(boot_block_read(d, pos << shift(d->storage->disks[d->slot].logical_size), m, 512));
        if (u16(m + 510) != 0xaa55)
            return BOOT_E_CORRUPT;
        uint64_t next = 0;
        for (unsigned i = 0; i < 4; ++i)
        {
            uint8_t *e = m + 446 + 16 * i;
            if (!e[4])
                continue;
            if (e[0] & 127)
                return BOOT_E_CORRUPT;
            uint64_t a = u32(e + 8), b = u32(e + 12);
            if (extended(e[4]))
            {
                if (next || !a || !range(a, b, extlen))
                    return BOOT_E_CORRUPT;
                next = ext + a;
            }
            else
            {
                if (!a || !b || !range(pos - ext + a, b, extlen))
                    return BOOT_E_CORRUPT;
                TRY(add(d, out, n, pos + a, b, (uint32_t)visited + 4, NULL));
            }
        }
        pos = next;
    }
    for (size_t i = 0; i < *n; ++i)
        copy(out[i].disk_identity, diskid, 16);
    return BOOT_OK;
}
boot_status_t boot_partitions(const struct boot_slice *d, struct boot_slice *out, size_t *count)
{
    if (!d || !out || !count)
        return BOOT_E_INVALID;
    uint8_t m[512];
    struct boot_slice parts[BOOT_PARTITIONS];
    size_t n = 0, capacity = *count;
    *count = 0;
    TRY(boot_block_read(d, 0, m, sizeof(m)));
    if (u16(m + 510) != 0xaa55)
        return BOOT_E_NOT_FOUND;
    int protective = 0;
    for (unsigned i = 0; i < 4; ++i)
        protective |= m[450 + 16 * i] == 0xee;
    if (protective)
        TRY(gpt(d, parts, &n));
    else
        TRY(mbr(d, parts, &n, m));
    if (!n)
        return BOOT_E_NOT_FOUND;
    if (n > capacity)
        return BOOT_E_NOMEM;
    copy(out, parts, n * sizeof(*out));
    *count = n;
    return BOOT_OK;
}
