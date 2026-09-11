/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_GRUB_CONTEXT_H
#define BOOT_GRUB_CONTEXT_H
#include <boot/storage.h>
#include <grub/fs.h>
#include <grub/misc.h>
#include <grub/mm.h>
struct boot_grub_context
{
    struct boot_grub_context *previous;
    grub_err_t error;
    boot_status_t provider_error;
    boot_status_t resource_error;
    unsigned reads;
    struct grub_disk disk;
    struct grub_device device;
};
void boot_grub_enter(struct boot_grub_context *, const struct boot_slice *);
boot_status_t boot_grub_leave(struct boot_grub_context *, grub_err_t);
void *boot_grub_alloc_raw(size_t);
void boot_grub_free_raw(void *);
extern grub_fs_t boot_grub_drivers[5];
void boot_grub_retain(struct boot_grub_context *, struct boot_grub_context *);
void boot_grub_release(struct boot_grub_context *);
#endif
