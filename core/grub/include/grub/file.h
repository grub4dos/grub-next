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
    void *boot_backing;
} *grub_file_t;
#define GRUB_FILE_SIZE_UNKNOWN UINT64_MAX
enum grub_file_type
{
    GRUB_FILE_TYPE_LOOPBACK = 1,
    GRUB_FILE_TYPE_NO_DECOMPRESS = 2
};
grub_file_t grub_file_open(const char *, enum grub_file_type);
grub_err_t grub_file_close(grub_file_t);
grub_off_t grub_file_seek(grub_file_t, grub_off_t);
grub_ssize_t grub_file_read(grub_file_t, void *, grub_size_t);
extern grub_disk_read_hook_t grub_file_progress_hook;
#include <grub/fs.h>
#endif
