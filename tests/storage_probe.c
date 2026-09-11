/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <boot/context.h>
#include <boot/storage_platform.h>
#include <boot/volume.h>
static struct boot_storage storage;
static struct boot_fs fs;
static struct boot_slice partitions[BOOT_PARTITIONS];
static uint8_t buffer[8192];
static void loop_probe(struct boot_context *c, struct boot_slice v)
{
    for (unsigned i = 0; i < 2; ++i)
    {
        if (boot_fs_mount(&fs, &v))
            return;
        struct boot_file f;
        if (boot_file_open(&fs, "/inner.img", &f))
            return;
        if (boot_loopback_add(i ? "loop1" : "loop0", &f, &v))
            boot_panic(c, "loopback-add", 0);
    }
    if (boot_fs_mount(&fs, &v))
        boot_panic(c, "loopback-mount", 0);
    struct boot_file f;
    if (boot_file_open(&fs, "/probe.bin", &f))
        boot_panic(c, "loopback-open", 0);
    size_t got;
    for (uint64_t off = 0; off < f.size;)
    {
        if (boot_file_read(&f, off, buffer, sizeof(buffer), &got) || !got)
            boot_panic(c, "loopback-read", 0);
        for (size_t j = 0; j < got; ++j)
            if (buffer[j] != (uint8_t)((off + j) * 17 + ((off + j) >> 8)))
                boot_panic(c, "loopback-data", 0);
        off += got;
    }
    boot_console(c, "BOOT:PASS:loopback-nested\r\n");
}
static void probe(struct boot_context *c, const struct boot_slice *v)
{
    boot_status_t s = boot_fs_mount(&fs, v);
    if (s)
        return;
    struct boot_file f;
    s = boot_file_open(&fs, "/probe.bin", &f);
    if (s == BOOT_E_NOT_FOUND || s == BOOT_E_UNSUPPORTED)
        return;
    if (s || f.size != 131209)
        boot_panic(c, "storage-open", 0);
    for (uint64_t off = 0; off < f.size;)
    {
        size_t got = 0;
        if (boot_file_read(&f, off, buffer, sizeof(buffer), &got) || !got)
            boot_panic(c, "storage-read", 0);
        for (size_t i = 0; i < got; ++i)
            if (buffer[i] != (uint8_t)((off + i) * 17 + ((off + i) >> 8)))
                boot_panic(c, "storage-content", 0);
        off += got;
    }
    size_t got = 1;
    if (boot_file_read(&f, f.size, buffer, 1, &got) || got ||
        boot_file_read(&f, UINT64_MAX, buffer, 1, &got) != BOOT_E_INVALID)
        boot_panic(c, "storage-eof", 0);
    const char *markers[] = {"", "BOOT:PASS:storage-fat\r\n", "BOOT:PASS:storage-iso9660\r\n",
                             "BOOT:PASS:storage-ext\r\n", "BOOT:PASS:storage-ntfs\r\n"};
    boot_console(c, markers[fs.kind]);
    s = boot_file_open(&fs, "/sparse.bin", &f);
    if (!s)
    {
        const uint64_t high = UINT64_C(5) * 1024 * 1024 * 1024;
        if (f.size != high + 26 || boot_file_read(&f, high, buffer, 26, &got) || got != 26)
            boot_panic(c, "storage-sparse-size", 0);
        for (unsigned i = 0; i < 26; ++i)
        {
            uint8_t expected =
                fs.kind == BOOT_FS_EXT && i >= 17 ? (uint8_t) "high-tail"[i - 17] : 0;
            if (buffer[i] != expected)
                boot_panic(c, "storage-sparse-content", 0);
        }
        boot_console(c, "BOOT:PASS:storage-sparse-high-offset\r\n");
    }
    else if (s != BOOT_E_NOT_FOUND)
        boot_panic(c, "storage-sparse-open", 0);
}
static boot_status_t volume_probe(void *opaque, const char *name)
{
    struct boot_context *c = opaque;
    struct boot_slice v;
    if (boot_volume_open(&storage, name, &v))
        boot_panic(c, "volume-open", 0);
    probe(c, &v);
    boot_console(c, "BOOT:PASS:volume:");
    boot_console(c, name);
    boot_console(c, "\r\n");
    return BOOT_OK;
}
void boot_storage_probe(struct boot_context *c)
{
    boot_status_t status = boot_platform_storage_scan(c, &storage);
    if (status)
    {
        boot_console(c, boot_status_string(status));
        boot_panic(c, "storage-scan", 0);
    }
    struct boot_slice old = {.storage = &storage, .generation = storage.generation, .slot = 0};
    if (boot_platform_storage_scan(c, &storage))
        boot_panic(c, "storage-rescan", 0);
    if (boot_slice_validate(&old) != BOOT_E_STALE)
        boot_panic(c, "storage-stale", 0);
    boot_console(c, "BOOT:PASS:storage-rescan-stale\r\n");
    size_t physical_count = storage.count;
    for (size_t i = 0; i < physical_count; ++i)
    {
        struct boot_block *d = &storage.disks[i];
        struct boot_slice v = {.storage = &storage,
                               .generation = storage.generation,
                               .slot = (uint32_t)i,
                               .size = d->blocks * d->logical_size};
        probe(c, &v);
        loop_probe(c, v);
        size_t count = BOOT_PARTITIONS;
        if (!boot_partitions(&v, partitions, &count))
            for (size_t j = 0; j < count; ++j)
                probe(c, &partitions[j]);
    }
    status = boot_diskfilter_scan(&storage);
    if (status)
    {
        boot_console(c, boot_status_string(status));
        boot_panic(c, "diskfilter-scan", 0);
    }
    if (boot_volume_list(&storage, volume_probe, c))
        boot_panic(c, "volume-list", 0);
    struct boot_slice volume_old = {.storage = &storage, .generation = storage.generation};
    if (boot_volume_reset(&storage) || boot_slice_validate(&volume_old) != BOOT_E_STALE)
        boot_panic(c, "volume-reset", 0);
    boot_console(c, "BOOT:PASS:volume-stale\r\n");
    boot_console(c, "BOOT:PASS:storage-ready\r\n");
}
