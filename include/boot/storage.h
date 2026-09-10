/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_STORAGE_H
#define BOOT_STORAGE_H
#include <boot/status.h>
#include <stddef.h>

#define BOOT_DISKS 32
#define BOOT_CACHE_LINES 8
#define BOOT_BLOCK_MAX 4096
#define BOOT_PARTITIONS 128
#define BOOT_NAME_MAX 768
struct boot_storage;
struct boot_block;
/* read consumes native logical blocks. Buffers satisfy io_alignment. */
struct boot_block_ops
{
    boot_status_t (*read)(void *, uint64_t, uint32_t, void *);
    boot_status_t (*validate)(void *);
};
struct boot_block
{
    struct boot_storage *storage;
    const struct boot_block_ops *ops;
    void *opaque;
    uint64_t blocks, generation;
    uint32_t logical_size, physical_size, io_alignment, max_blocks;
    uint32_t alignment_offset, slot;
    uint8_t identity[32]; /* Provider identity; never a display alias. */
    uint8_t identity_stable;
    const char *provider;
};
struct boot_cache_line
{
    uint64_t generation, lba;
    uint32_t slot, valid;
    _Alignas(4096) uint8_t data[BOOT_BLOCK_MAX];
};
struct boot_storage
{
    uint64_t generation;
    size_t count;
    unsigned busy;
    _Alignas(8) uint8_t provider_data[BOOT_DISKS][64];
    struct boot_block disks[BOOT_DISKS];
    struct boot_cache_line cache[BOOT_CACHE_LINES];
};
/* A value handle snapshots the generation, including for partitions/filters. */
struct boot_slice
{
    struct boot_storage *storage;
    uint64_t generation, offset, size;
    uint32_t slot, depth;
    uint8_t identity[16], disk_identity[16];
    uint32_t number;
};
/* Rescan invalidates every existing handle, including on failed enumeration. */
boot_status_t boot_storage_rescan(struct boot_storage *);
boot_status_t boot_block_add(struct boot_storage *, const struct boot_block *, struct boot_slice *);
boot_status_t boot_slice_validate(const struct boot_slice *);
boot_status_t boot_block_read(const struct boot_slice *, uint64_t, void *, size_t);
boot_status_t boot_filter_slice(const struct boot_slice *, uint64_t, uint64_t, struct boot_slice *);
boot_status_t boot_partitions(const struct boot_slice *, struct boot_slice *, size_t *);

enum boot_fs_kind
{
    BOOT_FS_FAT = 1,
    BOOT_FS_ISO9660,
    BOOT_FS_EXT,
    BOOT_FS_NTFS
};
struct boot_fs;
struct boot_file
{
    struct boot_fs *fs;
    uint64_t size, id, generation;
    uint32_t directory;
    char path[4096]; /* Reopened within a scoped upstream operation. */
};
struct boot_fs
{
    struct boot_slice slice;
    enum boot_fs_kind kind;
    char uuid[64]; /* Upstream textual UUID, empty when unavailable. */
    struct boot_file root;
};
typedef boot_status_t (*boot_dir_hook)(void *, const char *, const struct boot_file *);
boot_status_t boot_fs_mount(struct boot_fs *, const struct boot_slice *);
boot_status_t boot_file_open(struct boot_fs *, const char *, struct boot_file *);
boot_status_t boot_file_read(const struct boot_file *, uint64_t, void *, size_t, size_t *);
boot_status_t boot_file_list(const struct boot_file *, boot_dir_hook, void *);
#endif
