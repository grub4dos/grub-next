/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <grub/types.h>
#define GRUB_MAX_UTF8_PER_UTF16 3
grub_uint8_t *grub_utf16_to_utf8(grub_uint8_t *, const grub_uint16_t *, grub_size_t);
