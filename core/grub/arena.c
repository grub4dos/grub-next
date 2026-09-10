/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stddef.h>
#include <stdint.h>
struct chunk
{
    size_t size;
    unsigned used;
};
#define HEADER 16u
static _Alignas(16) unsigned char arena[5u * 1024 * 1024];
static unsigned initialized;
void *boot_grub_alloc_raw(size_t n)
{
    if (n > sizeof(arena) - HEADER - 15)
        return NULL;
    n = (n + 15) & ~(size_t)15;
    if (!initialized)
    {
        ((struct chunk *)arena)->size = sizeof(arena) - HEADER;
        initialized = 1;
    }
    for (size_t at = 0; at < sizeof(arena);)
    {
        struct chunk *p = (struct chunk *)(arena + at);
        if (!p->used && p->size >= n)
        {
            if (p->size >= n + HEADER + 16)
            {
                struct chunk *next = (struct chunk *)(arena + at + HEADER + n);
                next->used = 0;
                next->size = p->size - n - HEADER;
                p->size = n;
            }
            p->used = 1;
            return arena + at + HEADER;
        }
        at += HEADER + p->size;
    }
    return NULL;
}
void boot_grub_free_raw(void *v)
{
    ((struct chunk *)((unsigned char *)v - HEADER))->used = 0;
    for (size_t at = 0; at < sizeof(arena);)
    {
        struct chunk *p = (struct chunk *)(arena + at);
        size_t next_at = at + HEADER + p->size;
        if (!p->used && next_at < sizeof(arena))
        {
            struct chunk *next = (struct chunk *)(arena + next_at);
            if (!next->used)
            {
                p->size += HEADER + next->size;
                continue;
            }
        }
        at = next_at;
    }
}
