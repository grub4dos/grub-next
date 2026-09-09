/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stddef.h>
void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    for (size_t i = 0; i < n; ++i)
        d[i] = s[i];
    return dst;
}
void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = dst;
    for (size_t i = 0; i < n; ++i)
        d[i] = (unsigned char)c;
    return dst;
}
void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d < s)
        return memcpy(dst, src, n);
    while (n)
    {
        --n;
        d[n] = s[n];
    }
    return dst;
}
