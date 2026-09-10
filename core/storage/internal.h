/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_STORAGE_INTERNAL_H
#define BOOT_STORAGE_INTERNAL_H
#include <boot/storage.h>
#define TRY(expr)                                                                                  \
    do                                                                                             \
    {                                                                                              \
        boot_status_t result_ = (expr);                                                            \
        if (result_)                                                                               \
            return result_;                                                                        \
    } while (0)
static inline uint16_t u16(const void *v)
{
    const uint8_t *p = v;
    return p[0] | (uint16_t)p[1] << 8;
}
static inline uint32_t u32(const void *v)
{
    const uint8_t *p = v;
    return u16(p) | (uint32_t)u16(p + 2) << 16;
}
static inline uint64_t u64(const void *v)
{
    const uint8_t *p = v;
    return u32(p) | (uint64_t)u32(p + 4) << 32;
}
static inline void copy(void *v, const void *s, size_t n)
{
    uint8_t *p = v;
    const uint8_t *q = s;
    while (n--)
        *p++ = *q++;
}
static inline void zero(void *v, size_t n)
{
    uint8_t *p = v;
    while (n--)
        *p++ = 0;
}
static inline int equal(const void *a, const void *b, size_t n)
{
    const uint8_t *p = a, *q = b;
    while (n--)
        if (*p++ != *q++)
            return 0;
    return 1;
}
static inline int power2(uint32_t n)
{
    return n && !(n & (n - 1));
}
static inline int range(uint64_t at, uint64_t n, uint64_t end)
{
    return at <= end && n <= end - at;
}
static inline unsigned shift(uint32_t n)
{
    unsigned k = 0;
    while (n > 1)
    {
        n >>= 1;
        ++k;
    }
    return k;
}
/* No compiler-generated 64-bit division helpers in i386 freestanding code. */
static inline uint64_t divide(uint64_t n, uint32_t d, uint32_t *rem)
{
    uint64_t q = 0, r = 0;
    for (unsigned i = 64; i--;)
    {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d)
        {
            r -= d;
            q |= UINT64_C(1) << i;
        }
    }
    if (rem)
        *rem = (uint32_t)r;
    return q;
}
#endif
