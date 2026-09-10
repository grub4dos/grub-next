/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <boot/context.h>
#include <boot/storage_platform.h>
static struct boot_storage storage;
static struct boot_fs fs;
static struct boot_slice partitions[BOOT_PARTITIONS];
static uint8_t buffer[8192];
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
    for (size_t i = 0; i < storage.count; ++i)
    {
        struct boot_block *d = &storage.disks[i];
        struct boot_slice v = {.storage = &storage,
                               .generation = storage.generation,
                               .slot = (uint32_t)i,
                               .size = d->blocks * d->logical_size};
        probe(c, &v);
        size_t count = BOOT_PARTITIONS;
        if (!boot_partitions(&v, partitions, &count))
            for (size_t j = 0; j < count; ++j)
                probe(c, &partitions[j]);
    }
    boot_console(c, "BOOT:PASS:storage-ready\r\n");
}
