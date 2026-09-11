/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_GRUB_PARTITION_H
#define BOOT_GRUB_PARTITION_H
#include <grub/disk.h>
typedef struct grub_partition
{
    grub_disk_addr_t start, len;
} *grub_partition_t;
static inline grub_disk_addr_t grub_partition_get_start(grub_partition_t p)
{
    return p ? p->start : 0;
}
int grub_partition_iterate(grub_disk_t, int (*)(grub_disk_t, grub_partition_t, void *), void *);
#endif
