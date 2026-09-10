/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Independent firmware-protocol mock, including media changes on cache hits. */
#include <assert.h>
#include <boot/efi.h>
#include <boot/storage_platform.h>
#include <stdio.h>
#include <string.h>
struct mock_media
{
    uint32_t id;
    uint8_t removable, present, partition, readonly, caching;
    uint32_t block_size, alignment;
    uint64_t last, lowest_aligned;
    uint32_t logical_per_physical, optimal;
};
struct mock_io
{
    uint64_t revision;
    struct mock_media *media;
    void *reset;
    boot_efi_status (*read)(void *, uint32_t, uint64_t, uintptr_t, void *);
    void *write, *flush;
};
static struct mock_media media = {.id = 7,
                                  .present = 1,
                                  .block_size = 512,
                                  .alignment = 4096,
                                  .last = 31,
                                  .logical_per_physical = 8};
static unsigned reads, releases;
static int fail_scan;
static boot_efi_status read_blocks(void *p, uint32_t id, uint64_t lba, uintptr_t size, void *buffer)
{
    (void)p;
    assert(id == media.id);
    assert(lba <= media.last);
    assert(size == 512);
    assert(!((uintptr_t)buffer & 4095));
    ++reads;
    memset(buffer, (int)media.id, size);
    return 0;
}
static struct mock_io io = {.revision = 0x20001, .media = &media, .read = read_blocks};
static boot_efi_status handles(uint32_t type, const void *guid, void *key, uintptr_t *count,
                               void ***out)
{
    assert(type == 2 && ((const uint8_t *)guid)[0] == 0x21 && !key);
    if (fail_scan)
        return (UINTPTR_MAX ^ (UINTPTR_MAX >> 1)) | 7;
    static void *array[1] = {&io};
    *out = array;
    *count = 1;
    return 0;
}
static boot_efi_status protocol(void *handle, const void *guid, void **out)
{
    assert(handle == &io);
    static uint8_t path[8] = {1, 1, 4, 0, 127, 255, 4, 0};
    *out = ((const uint8_t *)guid)[0] == 0x21 ? (void *)&io : path;
    return 0;
}
static boot_efi_status release(void *p)
{
    (void)p;
    ++releases;
    return 0;
}
int main(void)
{
    static struct boot_storage storage;
    struct boot_efi_services services = {
        .locate_handle_buffer = handles, .handle_protocol = protocol, .free_pool = release};
    struct boot_efi_system system = {.services = &services};
    struct boot_context context = {.firmware_table = &system};
    assert(!boot_platform_storage_scan(&context, &storage));
    assert(storage.count == 1 && releases == 1 && storage.disks[0].physical_size == 4096);
    assert(storage.disks[0].identity_stable);
    uint8_t identity[32];
    memcpy(identity, storage.disks[0].identity, 32);
    struct boot_slice old = {.storage = &storage, .generation = storage.generation, .size = 16384};
    uint8_t buffer[13];
    assert(!boot_block_read(&old, 3, buffer, 13) && buffer[0] == 7 && reads == 1);
    assert(!boot_block_read(&old, 3, buffer, 13) && reads == 1);
    ++media.id;
    assert(boot_block_read(&old, 3, buffer, 13) == BOOT_E_STALE && reads == 1);
    assert(!boot_platform_storage_scan(&context, &storage));
    assert(boot_block_read(&old, 3, buffer, 13) == BOOT_E_STALE);
    assert(!memcmp(identity, storage.disks[0].identity, 32));
    old.generation = storage.generation;
    assert(!boot_block_read(&old, 3, buffer, 13) && buffer[0] == 8 && reads == 2);
    media.present = 0;
    assert(boot_block_read(&old, 3, buffer, 13) == BOOT_E_STALE);
    media.present = 1;
    fail_scan = 1;
    assert(boot_platform_storage_scan(&context, &storage) == BOOT_E_IO);
    assert(boot_block_read(&old, 3, buffer, 13) == BOOT_E_STALE && storage.count == 0);
    puts("PASS: EFI media replacement, aligned read, stable path identity and failed rescan");
    return 0;
}
