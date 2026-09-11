/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "context.h"
#include <grub/charset.h>
#include <stdarg.h>

/* Only this pointer is ambient. Each public operation owns its error and
 * allocations; nested directory callbacks restore the caller's context. */
static struct boot_grub_context *current;
grub_fs_t boot_grub_drivers[5];
grub_disk_read_hook_t grub_file_progress_hook;
struct allocation
{
    struct allocation *next, *previous;
    struct boot_grub_context *owner;
    size_t size;
};
static struct allocation *allocations;
static size_t allocated;
#define BOOT_GRUB_HEAP_LIMIT (4u * 1024 * 1024)
void boot_grub_retain(struct boot_grub_context *from, struct boot_grub_context *to)
{
    for (struct allocation *p = allocations; p; p = p->next)
        if (p->owner == from)
            p->owner = to;
}
void boot_grub_release(struct boot_grub_context *owner)
{
    for (struct allocation *p = allocations, *next; p; p = next)
    {
        next = p->next;
        if (p->owner == owner)
            grub_free(p + 1);
    }
}

grub_err_t *boot_grub_error_slot(void)
{
    return &current->error;
}
grub_err_t grub_error(grub_err_t error, const char *format, ...)
{
    (void)format;
    if (error == GRUB_ERR_OUT_OF_MEMORY)
        current->resource_error = BOOT_E_NOMEM;
    return current->error = error;
}
void boot_grub_enter(struct boot_grub_context *ctx, const struct boot_slice *slice)
{
    grub_memset(ctx, 0, sizeof(*ctx));
    ctx->previous = current;
    ctx->disk.slice = slice;
    ctx->device.disk = &ctx->disk;
    current = ctx;
}
boot_status_t boot_grub_leave(struct boot_grub_context *ctx, grub_err_t error)
{
    for (struct allocation *p = allocations, *next; p; p = next)
    {
        next = p->next;
        if (p->owner == ctx)
            grub_free(p + 1);
    }
    current = ctx->previous;
    if (ctx->provider_error)
        return ctx->provider_error;
    if (ctx->resource_error)
        return ctx->resource_error;
    if (!error)
        error = ctx->error;
    switch (error)
    {
    case GRUB_ERR_NONE:
        return BOOT_OK;
    case GRUB_ERR_OUT_OF_MEMORY:
        return BOOT_E_NOMEM;
    case GRUB_ERR_FILE_NOT_FOUND:
    case GRUB_ERR_UNKNOWN_DEVICE:
        return BOOT_E_NOT_FOUND;
    case GRUB_ERR_BAD_FILENAME:
    case GRUB_ERR_BAD_ARGUMENT:
    case GRUB_ERR_STILL_REFERENCED:
    case GRUB_ERR_BAD_FILE_TYPE:
        return BOOT_E_INVALID;
    case GRUB_ERR_NOT_IMPLEMENTED_YET:
        return BOOT_E_UNSUPPORTED;
    case GRUB_ERR_READ_ERROR:
        return BOOT_E_IO;
    default:
        return BOOT_E_CORRUPT;
    }
}
void *grub_malloc(size_t n)
{
    if (!n)
        n = 1;
    if (n > BOOT_GRUB_HEAP_LIMIT - allocated || n > SIZE_MAX - sizeof(struct allocation))
    {
        grub_error(GRUB_ERR_OUT_OF_MEMORY, "allocation limit");
        return NULL;
    }
    struct allocation *p = boot_grub_alloc_raw(sizeof(*p) + n);
    if (!p)
    {
        grub_error(GRUB_ERR_OUT_OF_MEMORY, "allocation failed");
        return NULL;
    }
    p->size = n;
    p->owner = current;
    p->previous = NULL;
    p->next = allocations;
    if (allocations)
        allocations->previous = p;
    allocations = p;
    allocated += n;
    return p + 1;
}
void grub_free(void *v)
{
    if (!v)
        return;
    struct allocation *p = (struct allocation *)v - 1;
    if (p->next)
        p->next->previous = p->previous;
    if (p->previous)
        p->previous->next = p->next;
    else
        allocations = p->next;
    allocated -= p->size;
    boot_grub_free_raw(p);
}
void *grub_zalloc(size_t n)
{
    void *p = grub_malloc(n);
    if (p)
        grub_memset(p, 0, n);
    return p;
}
void *grub_calloc(size_t n, size_t size)
{
    if (__builtin_mul_overflow(n, size, &n))
    {
        grub_error(GRUB_ERR_OUT_OF_MEMORY, "allocation overflow");
        return NULL;
    }
    return grub_zalloc(n);
}
void *grub_realloc(void *v, size_t n)
{
    void *p = grub_malloc(n);
    if (p && v)
    {
        size_t old = ((struct allocation *)v - 1)->size;
        grub_memcpy(p, v, grub_min(old, n));
        grub_free(v);
    }
    return p;
}
grub_err_t grub_disk_read(grub_disk_t disk, grub_disk_addr_t sector, grub_off_t offset, size_t n,
                          void *buffer)
{
    uint64_t at;
    if (++current->reads > 1048576 || sector > (UINT64_MAX >> 9) ||
        __builtin_add_overflow(sector << 9, offset, &at))
        return grub_error(GRUB_ERR_OUT_OF_RANGE, "read limit");
    if (!disk->slice)
        return boot_grub_virtual_read(disk, at, buffer, n);
    if (at > disk->slice->size || n > disk->slice->size - at)
        return grub_error(GRUB_ERR_OUT_OF_RANGE, "slice range");
    boot_status_t status = boot_block_read(disk->slice, at, buffer, n);
    if (status)
    {
        if (!current->provider_error)
            current->provider_error = status;
        return grub_error(GRUB_ERR_READ_ERROR, "provider read");
    }
    if (disk->read_hook)
        return disk->read_hook(sector, (unsigned)offset, (unsigned)n, buffer, disk->read_hook_data);
    return GRUB_ERR_NONE;
}
void grub_fs_register(grub_fs_t fs)
{
    static const char *const names[] = {"", "fat", "iso9660", "ext2", "ntfs"};
    for (unsigned i = 1; i < ARRAY_SIZE(names); ++i)
        if (!grub_strcmp(names[i], fs->name))
            boot_grub_drivers[i] = fs;
}
void grub_fs_unregister(grub_fs_t fs)
{
    (void)fs;
}

void *grub_memcpy(void *v, const void *s, size_t n)
{
    unsigned char *p = v;
    const unsigned char *q = s;
    while (n--)
        *p++ = *q++;
    return v;
}
void *grub_memmove(void *v, const void *s, size_t n)
{
    unsigned char *p = v;
    const unsigned char *q = s;
    if ((uintptr_t)p > (uintptr_t)q)
        while (n)
        {
            --n;
            p[n] = q[n];
        }
    else
        grub_memcpy(v, s, n);
    return v;
}
void *grub_memset(void *v, int c, size_t n)
{
    unsigned char *p = v;
    while (n--)
        *p++ = (unsigned char)c;
    return v;
}
int grub_memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = a, *q = b;
    while (n--)
    {
        if (*p != *q)
            return (int)*p - *q;
        ++p;
        ++q;
    }
    return 0;
}
size_t grub_strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        ++n;
    return n;
}
int grub_strncmp(const char *a, const char *b, size_t n)
{
    while (n--)
    {
        if (*a != *b)
            return (unsigned char)*a - (unsigned char)*b;
        if (!*a)
            break;
        ++a;
        ++b;
    }
    return 0;
}
int grub_strcmp(const char *a, const char *b)
{
    return grub_strncmp(a, b, SIZE_MAX);
}
int grub_strcasecmp(const char *a, const char *b)
{
    while (*a && grub_tolower((unsigned char)*a) == grub_tolower((unsigned char)*b))
    {
        ++a;
        ++b;
    }
    return grub_tolower((unsigned char)*a) - grub_tolower((unsigned char)*b);
}
char *grub_strcpy(char *a, const char *b)
{
    return grub_memcpy(a, b, grub_strlen(b) + 1);
}
char *grub_strndup(const char *s, size_t n)
{
    size_t len = 0;
    while (len < n && s[len])
        ++len;
    char *p = grub_malloc(len + 1);
    if (p)
    {
        grub_memcpy(p, s, len);
        p[len] = 0;
    }
    return p;
}
char *grub_strdup(const char *s)
{
    return grub_strndup(s, SIZE_MAX);
}
char *grub_strrchr(const char *s, int c)
{
    const char *last = NULL;
    do
    {
        if ((unsigned char)*s == (unsigned char)c)
            last = s;
    } while (*s++);
    return (char *)last;
}
uint64_t grub_divmod64(uint64_t n, uint64_t d, uint64_t *remainder)
{
    uint64_t q = 0, r = 0;
    if (!d)
    {
        if (remainder)
            *remainder = 0;
        return 0;
    }
    for (unsigned i = 64; i--;)
    {
        unsigned carry = (unsigned)(r >> 63);
        r = (r << 1) | ((n >> i) & 1);
        if (carry || r >= d)
        {
            r -= d;
            q |= UINT64_C(1) << i;
        }
    }
    if (remainder)
        *remainder = r;
    return q;
}
uint8_t *grub_utf16_to_utf8(uint8_t *out, const uint16_t *in, size_t n)
{
    while (n--)
    {
        uint32_t c = *in++;
        if (c >= 0xd800 && c <= 0xdbff && n && *in >= 0xdc00 && *in <= 0xdfff)
        {
            c = 0x10000 + ((c - 0xd800) << 10) + *in++ - 0xdc00;
            --n;
        }
        else if (c >= 0xd800 && c <= 0xdfff)
            c = 0xfffd;
        unsigned bytes = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
        if (bytes == 1)
            *out++ = (uint8_t)c;
        else
        {
            *out++ = (uint8_t)((bytes == 2   ? 0xc0
                                : bytes == 3 ? 0xe0
                                             : 0xf0) |
                               (c >> (6 * (bytes - 1))));
            for (unsigned j = bytes - 1; j--;)
                *out++ = (uint8_t)(0x80 | ((c >> (6 * j)) & 63));
        }
    }
    return out;
}
/* The imported fs_uuid callbacks use only %c and fixed-width %x/%llx. */
char *grub_xasprintf(const char *format, ...)
{
    char output[128];
    size_t n = 0;
    va_list args;
    va_start(args, format);
    while (*format && n < sizeof(output) - 1)
    {
        if (*format++ != '%')
        {
            output[n++] = format[-1];
            continue;
        }
        unsigned width = 0, wide = 0;
        while (*format >= '0' && *format <= '9')
            width = width * 10 + (unsigned)(*format++ - '0');
        while (*format == 'l')
        {
            ++wide;
            ++format;
        }
        if (*format == 's')
        {
            const char *s = va_arg(args, const char *);
            while (*s && n < sizeof(output) - 1)
                output[n++] = *s++;
        }
        else if (*format == 'd' || *format == 'u')
        {
            unsigned value = va_arg(args, unsigned);
            char digits[10];
            unsigned count = 0;
            do
            {
                digits[count++] = (char)('0' + value % 10);
                value /= 10;
            } while (value);
            while (count && n < sizeof(output) - 1)
                output[n++] = digits[--count];
        }
        else if (*format == 'c')
            output[n++] = (char)va_arg(args, int);
        else if (*format == 'x')
        {
            uint64_t value =
                wide == 2 ? va_arg(args, unsigned long long) : va_arg(args, unsigned int);
            unsigned digits = 1;
            for (uint64_t v = value >> 4; v; v >>= 4)
                ++digits;
            if (width > digits)
                digits = width;
            if (digits > 16 || n + digits >= sizeof(output))
                break;
            while (digits--)
                output[n++] = "0123456789abcdef"[(value >> (digits * 4)) & 15];
        }
        else
            break;
        ++format;
    }
    va_end(args);
    output[n] = 0;
    return grub_strdup(output);
}
