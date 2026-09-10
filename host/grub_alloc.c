/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Separate allocations give ASan real allocation boundaries for vendor code. */
#include <stdlib.h>
void *boot_grub_alloc_raw(size_t n)
{
    return malloc(n);
}
void boot_grub_free_raw(void *p)
{
    free(p);
}
