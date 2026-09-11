/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_GRUB_DISK_H
#define BOOT_GRUB_DISK_H
#include <grub/err.h>
#include <grub/symbol.h>
#include <grub/types.h>
#define GRUB_DISK_SECTOR_BITS 9
#define GRUB_DISK_SECTOR_SIZE 512
#define GRUB_DISK_CACHE_BITS 6
#define GRUB_DISK_SIZE_UNKNOWN UINT64_MAX
#define GRUB_MDRAID_MAX_DISKS 32
typedef enum
{
    GRUB_DISK_PULL_NONE,
    GRUB_DISK_PULL_RESCAN,
    GRUB_DISK_PULL_MAX
} grub_disk_pull_t;
enum
{
    GRUB_DISK_DEVICE_HOST_ID,
    GRUB_DISK_DEVICE_LOOPBACK_ID,
    GRUB_DISK_DEVICE_DISKFILTER_ID,
    GRUB_DISK_DEVICE_CRYPTODISK_ID
};
typedef int (*grub_disk_dev_iterate_hook_t)(const char *, void *);
struct grub_disk;
typedef struct grub_disk_dev
{
    const char *name;
    int id;
    int (*disk_iterate)(grub_disk_dev_iterate_hook_t, void *, grub_disk_pull_t);
    grub_err_t (*disk_open)(const char *, struct grub_disk *);
    void (*disk_close)(struct grub_disk *);
    grub_err_t (*disk_read)(struct grub_disk *, grub_disk_addr_t, grub_size_t, char *);
    grub_err_t (*disk_write)(struct grub_disk *, grub_disk_addr_t, grub_size_t, const char *);
    struct grub_disk_dev *next;
} *grub_disk_dev_t;
extern grub_disk_dev_t grub_disk_dev_list;
typedef grub_err_t (*grub_disk_read_hook_t)(grub_disk_addr_t, unsigned, unsigned, const char *,
                                            void *);
typedef struct grub_disk
{
    grub_disk_read_hook_t read_hook;
    void *read_hook_data;
    const struct boot_slice *slice;
    const char *name;
    grub_disk_dev_t dev;
    struct grub_partition *partition;
    grub_uint64_t total_sectors;
    unsigned long id;
    unsigned max_agglomerate, log_sector_size;
    void *data;
} *grub_disk_t;
void grub_disk_dev_register(grub_disk_dev_t);
void grub_disk_dev_unregister(grub_disk_dev_t);
grub_disk_t grub_disk_open(const char *);
void grub_disk_close(grub_disk_t);
grub_disk_addr_t grub_disk_native_sectors(grub_disk_t);
grub_err_t boot_grub_virtual_read(grub_disk_t, grub_uint64_t, void *, grub_size_t);
grub_err_t grub_disk_read(grub_disk_t, grub_disk_addr_t, grub_off_t, grub_size_t, void *);
#endif
