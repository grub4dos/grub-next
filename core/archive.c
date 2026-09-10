/* SPDX-License-Identifier: GPL-3.0-or-later */
/* newc layout/alignment reference: GNU GRUB newc.c and cpio_common.c.
   This bounded memory reader is a new implementation, not a disk FS adapter. */
#include <boot/archive.h>
static int equal(const char *a, const char *b)
{
    while (*a && *a == *b)
    {
        ++a;
        ++b;
    }
    return *a == *b;
}
static int hex(const uint8_t *p, uint32_t *v)
{
    *v = 0;
    for (unsigned i = 0; i < 8; ++i)
    {
        unsigned c = p[i], n;
        if (c >= '0' && c <= '9')
            n = c - '0';
        else if (c >= 'a' && c <= 'f')
            n = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            n = c - 'A' + 10;
        else
            return 0;
        *v = (*v << 4) | n;
    }
    return 1;
}
static int path_valid(const uint8_t *p, size_t n)
{
    if (n < 2 || n > 256 || p[n - 1])
        return 0;
    size_t start = 0;
    for (size_t i = 0; i < n; ++i)
    {
        if (i == n - 1 || p[i] == '/')
        {
            size_t len = i - start;
            if (!len || (len == 1 && p[start] == '.') ||
                (len == 2 && p[start] == '.' && p[start + 1] == '.'))
                return 0;
            start = i + 1;
        }
        else if (!((p[i] >= 'a' && p[i] <= 'z') || (p[i] >= 'A' && p[i] <= 'Z') ||
                   (p[i] >= '0' && p[i] <= '9') || p[i] == '.' || p[i] == '_' || p[i] == '-'))
            return 0;
    }
    return 1;
}
static boot_status_t parse(struct boot_archive *a, const uint8_t *p, size_t size)
{
    size_t off = 0;
    while (size - off >= 110)
    {
        const uint8_t *h = p + off;
        if (h[0] != '0' || h[1] != '7' || h[2] != '0' || h[3] != '7' || h[4] != '0' || h[5] != '1')
            return BOOT_E_INVALID;
        uint32_t v[13];
        for (unsigned i = 0; i < 13; ++i)
            if (!hex(h + 6 + i * 8, &v[i]))
                return BOOT_E_INVALID;
        size_t ns = v[11], bytes = v[6];
        off += 110;
        if (!ns || ns > size - off || ns > 256)
            return BOOT_E_INVALID;
        const char *name = (const char *)(p + off);
        if (p[off + ns - 1])
            return BOOT_E_INVALID;
        off += ns;
        size_t pad = (4 - (off & 3)) & 3;
        if (pad > size - off)
            return BOOT_E_INVALID;
        off += pad;
        if (bytes > size - off || v[12])
            return BOOT_E_INVALID;
        const uint8_t *data = p + off;
        off += bytes;
        pad = (4 - (off & 3)) & 3;
        if (pad > size - off)
            return BOOT_E_INVALID;
        off += pad;
        if (ns == 11 && equal(name, "TRAILER!!!"))
        {
            if (bytes || v[1])
                return BOOT_E_INVALID;
            while (off < size)
                if (p[off++])
                    return BOOT_E_INVALID;
            return BOOT_OK;
        }
        if (!path_valid((const uint8_t *)name, ns) ||
            ((v[1] & 0170000) != 0100000 && (v[1] & 0170000) != 0040000) ||
            ((v[1] & 0170000) == 0100000 && v[4] != 1) || ((v[1] & 0170000) == 0040000 && bytes))
            return BOOT_E_INVALID;
        if (a->count == BOOT_ARCHIVE_MAX_FILES)
            return BOOT_E_NOMEM;
        for (size_t i = 0; i < a->count; ++i)
            if (equal(a->files[i].name, name))
                return BOOT_E_INVALID;
        a->files[a->count++] = (struct boot_archive_file){name, data, bytes, v[1]};
    }
    return BOOT_E_INVALID; /* A complete trailer is mandatory. */
}
boot_status_t boot_archive_open(struct boot_archive *a, const void *data, size_t size)
{
    if (!a)
        return BOOT_E_INVALID;
    a->count = 0;
    if (!data || size > BOOT_ARCHIVE_MAX_SIZE)
        return BOOT_E_INVALID;
    boot_status_t s = parse(a, data, size);
    if (s)
        a->count = 0; /* No partially validated resources become visible. */
    return s;
}
boot_status_t boot_archive_find(const struct boot_archive *a, const char *name,
                                struct boot_archive_file *out)
{
    if (!a || !name || !out || a->count > BOOT_ARCHIVE_MAX_FILES)
        return BOOT_E_INVALID;
    *out = (struct boot_archive_file){0};
    for (size_t i = 0; i < a->count; ++i)
        if (equal(a->files[i].name, name) && (a->files[i].mode & 0170000) == 0100000)
        {
            *out = a->files[i];
            return BOOT_OK;
        }
    return BOOT_E_INVALID;
}
boot_status_t boot_archive_verify(const struct boot_archive *a)
{
    struct boot_archive_file manifest;
    if (boot_archive_find(a, "manifest.sha256", &manifest))
        return BOOT_E_INVALID;
    size_t off = 0;
    for (size_t i = 0; i < a->count; ++i)
    {
        const struct boot_archive_file *f = &a->files[i];
        if (equal(f->name, "manifest.sha256") || (f->mode & 0170000) == 0040000)
            continue;
        if (manifest.size - off < 66)
            return BOOT_E_INVALID;
        uint8_t hash[32];
        boot_sha256(f->data, f->size, hash);
        const char *digits = "0123456789abcdef";
        for (size_t j = 0; j < 32; ++j)
            if (manifest.data[off + j * 2] != digits[hash[j] >> 4] ||
                manifest.data[off + j * 2 + 1] != digits[hash[j] & 15])
                return BOOT_E_INVALID;
        off += 64;
        if (manifest.data[off++] != ' ' || manifest.data[off++] != ' ')
            return BOOT_E_INVALID;
        for (const char *n = f->name; *n; ++n)
            if (off == manifest.size || manifest.data[off++] != (uint8_t)*n)
                return BOOT_E_INVALID;
        if (off == manifest.size || manifest.data[off++] != '\n')
            return BOOT_E_INVALID;
    }
    return off == manifest.size ? BOOT_OK : BOOT_E_INVALID;
}
