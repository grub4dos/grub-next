/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_GRUB_FS_H
#define BOOT_GRUB_FS_H
#include <grub/dl.h>
#include <grub/file.h>
struct grub_dirhook_info
{
    unsigned dir : 1, mtimeset : 1, case_insensitive : 1, inodeset : 1;
    grub_int64_t mtime;
    grub_uint64_t inode;
};
typedef int (*grub_fs_dir_hook_t)(const char *, const struct grub_dirhook_info *, void *);
typedef struct grub_fs
{
    struct grub_fs *next;
    const char *name;
    grub_dl_t mod;
    grub_err_t (*fs_dir)(grub_device_t, const char *, grub_fs_dir_hook_t, void *);
    grub_err_t (*fs_open)(grub_file_t, const char *);
    grub_ssize_t (*fs_read)(grub_file_t, char *, grub_size_t);
    grub_err_t (*fs_close)(grub_file_t);
    grub_err_t (*fs_label)(grub_device_t, char **);
    grub_err_t (*fs_uuid)(grub_device_t, char **);
    grub_err_t (*fs_mtime)(grub_device_t, grub_int64_t *);
} *grub_fs_t;
void grub_fs_register(grub_fs_t);
void grub_fs_unregister(grub_fs_t);
#endif
