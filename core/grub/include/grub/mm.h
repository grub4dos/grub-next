/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_GRUB_MM_H
#define BOOT_GRUB_MM_H
#include <grub/types.h>
void *grub_malloc(grub_size_t);
void *grub_zalloc(grub_size_t);
void *grub_calloc(grub_size_t, grub_size_t);
void *grub_realloc(void *, grub_size_t);
void grub_free(void *);
#endif
