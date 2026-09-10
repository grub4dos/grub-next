/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_GRUB_DISK_H
#define BOOT_GRUB_DISK_H
#include <grub/err.h>
#include <grub/symbol.h>
#include <grub/types.h>
#define GRUB_DISK_SECTOR_BITS 9
#define GRUB_DISK_SECTOR_SIZE 512
typedef grub_err_t (*grub_disk_read_hook_t)(grub_disk_addr_t, unsigned, unsigned, const char *,
                                            void *);
typedef struct grub_disk
{
    grub_disk_read_hook_t read_hook;
    void *read_hook_data;
    const struct boot_slice *slice;
} *grub_disk_t;
grub_err_t grub_disk_read(grub_disk_t, grub_disk_addr_t, grub_off_t, grub_size_t, void *);
#endif
