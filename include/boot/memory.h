/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_MEMORY_H
#define BOOT_MEMORY_H
#include <boot/status.h>
#include <stddef.h>
#define BOOT_REGIONS 256
#define BOOT_ALLOCATIONS 128
typedef uint64_t boot_phys_t;
enum boot_memory_type
{
    BOOT_MEM_FREE,
    BOOT_MEM_RESERVED,
    BOOT_MEM_ACPI,
    BOOT_MEM_NVS,
    BOOT_MEM_BAD,
    BOOT_MEM_BS,
    BOOT_MEM_RT,
    BOOT_MEM_LOADER
};
enum boot_owner
{
    BOOT_BL = 1,
    BOOT_LOADER,
    BOOT_RESIDENT
};
struct boot_region
{
    boot_phys_t base, size;
    uint32_t type;
};
struct boot_allocation
{
    boot_phys_t base, size;
    enum boot_owner owner;
    unsigned transaction;
};
struct boot_memory
{
    struct boot_region regions[BOOT_REGIONS];
    size_t count;
    struct boot_allocation allocations[BOOT_ALLOCATIONS];
    size_t used;
    unsigned transaction, next_transaction;
    unsigned physical_bits;
    int frozen;
    uint64_t page_size, resident_page_size;
    boot_status_t (*refresh)(struct boot_memory *);
    boot_status_t (*reserve_pages)(uint64_t, uint64_t, enum boot_owner);
    boot_status_t (*free_pages)(uint64_t, uint64_t);
};
boot_status_t boot_memory_add(struct boot_memory *, boot_phys_t, uint64_t, uint32_t);
boot_status_t boot_memory_reserve(struct boot_memory *, boot_phys_t, uint64_t, enum boot_owner);
boot_status_t boot_memory_alloc(struct boot_memory *, enum boot_owner, uint64_t size,
                                uint64_t alignment, boot_phys_t minimum, boot_phys_t maximum,
                                boot_phys_t *);
boot_status_t boot_memory_release(struct boot_memory *, boot_phys_t);
boot_status_t boot_loader_begin(struct boot_memory *);
boot_status_t boot_loader_abort(struct boot_memory *);
boot_status_t boot_loader_commit(struct boot_memory *);
boot_status_t boot_memory_export(const struct boot_memory *, struct boot_region *, size_t *);
boot_status_t boot_memory_access(const struct boot_memory *, boot_phys_t, uint64_t);
#endif
