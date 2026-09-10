/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_GRUB_FILE_H
#define BOOT_GRUB_FILE_H
#include <grub/disk.h>
typedef struct grub_device
{
    grub_disk_t disk;
} *grub_device_t;
typedef struct grub_file
{
    grub_device_t device;
    struct grub_fs *fs;
    grub_off_t offset, size;
    void *data;
    grub_disk_read_hook_t read_hook;
    void *read_hook_data;
} *grub_file_t;
extern grub_disk_read_hook_t grub_file_progress_hook;
#include <grub/fs.h>
#endif
