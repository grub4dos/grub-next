/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_GRUB_CRYPTO_H
#define BOOT_GRUB_CRYPTO_H
#include <grub/types.h>
static inline void grub_crypto_xor(void *out, const void *a, const void *b, grub_size_t n)
{
    grub_uint8_t *p = out;
    const grub_uint8_t *x = a, *y = b;
    while (n--)
        *p++ = *x++ ^ *y++;
}
#endif
