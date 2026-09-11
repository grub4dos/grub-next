/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <boot/host_block.h>
#include <boot/volume.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct boot_storage storage;
static struct boot_fs fs;
static struct boot_host_block hosts[BOOT_DISKS];
static struct boot_slice roots[BOOT_DISKS];
static boot_status_t list(void *arg, const char *name)
{
    (void)arg;
    fprintf(stderr, "volume: %s\n", name);
    return BOOT_OK;
}
int main(int argc, char **argv)
{
    if (argc < 5 || argc > BOOT_DISKS + 4)
        return 2;
    assert(boot_storage_rescan(&storage) == BOOT_OK);
    for (int i = 4; i < argc; ++i)
    {
        hosts[i - 4].fd = -1;
        assert(boot_host_block_open(&storage, &hosts[i - 4], argv[i],
                                    (uint32_t)strtoul(argv[1], NULL, 0), &roots[i - 4]) == BOOT_OK);
    }
    struct boot_slice v = roots[0];
    boot_status_t status;
    if (!strncmp(argv[2], "loop", 4) || !strcmp(argv[2], "nested"))
    {
        unsigned levels = !strcmp(argv[2], "nested") ? 2 : 1;
        for (unsigned i = 0; i < levels; ++i)
        {
            assert(boot_fs_mount(&fs, &v) == BOOT_OK);
            struct boot_file f;
            assert(boot_file_open(&fs, "/inner.img", &f) == BOOT_OK);
            const char *name = i ? "loop1" : "loop0";
            assert(boot_loopback_add(name, &f, &v) == BOOT_OK);
            struct boot_slice duplicate;
            assert(boot_loopback_add(name, &f, &duplicate) == BOOT_E_INVALID);
            /* The session must own a copy, not the caller's stack/fs. */
            memset(&fs, 0, sizeof(fs));
            memset(&f, 0, sizeof(f));
        }
        if (!strcmp(argv[2], "loop-lvm"))
        {
            assert(boot_diskfilter_scan(&storage) == BOOT_OK);
            assert(boot_volume_open(&storage, "lvm/fixture-data", &v) == BOOT_OK);
        }
    }
    else
    {
        assert(boot_diskfilter_scan(&storage) == BOOT_OK);
        assert(boot_volume_list(&storage, list, NULL) == BOOT_OK);
        status = boot_volume_open(&storage, argv[2], &v);
        if (status)
        {
            fprintf(stderr, "open status=%u\n", status);
            assert(boot_volume_reset(&storage) == BOOT_OK);
            for (int i = 4; i < argc; ++i)
                boot_host_block_close(&hosts[i - 4]);
            return 1;
        }
    }
    int physical = 1;
    assert(boot_slice_physical(&v, &physical) == BOOT_OK && !physical);
    struct boot_slice sub;
    assert(boot_filter_slice(&v, 0, 512, &sub) == BOOT_OK);
    assert(boot_slice_physical(&sub, &physical) == BOOT_OK && !physical);
    status = boot_fs_mount(&fs, &v);
    if (status)
    {
        uint8_t sector[512];
        fprintf(stderr, "mount status=%u size=%llu read=%u\n", status, (unsigned long long)v.size,
                boot_block_read(&v, 0, sector, sizeof(sector)));
        for (unsigned i = 0; i < 64; ++i)
            fprintf(stderr, "%02x", sector[i]);
        fprintf(stderr, "\n");
    }
    assert(status == BOOT_OK);
    struct boot_file file;
    assert(boot_file_open(&fs, argv[3], &file) == BOOT_OK);
    struct boot_slice tail;
    assert(boot_loopback_add("loop-tail", &file, &tail) == BOOT_OK);
    uint8_t last[512];
    uint64_t last_at = file.size & ~UINT64_C(511);
    assert(boot_block_read(&tail, last_at, last, sizeof(last)) == BOOT_OK);
    for (size_t i = (size_t)(file.size & 511); i < sizeof(last); ++i)
        assert(last[i] == 0);
    uint8_t buf[8192];
    for (uint64_t off = 0; off < file.size;)
    {
        size_t got;
        assert(boot_file_read(&file, off, buf, sizeof(buf), &got) == BOOT_OK && got);
        assert(fwrite(buf, 1, got, stdout) == got);
        off += got;
    }
    /* Exercise partial sector and cache collisions, not just sequential I/O. */
    uint8_t a[1025], b[1025];
    assert(boot_block_read(&v, 499, a, sizeof(a)) == BOOT_OK);
    assert(boot_block_read(&v, 4096, b, sizeof(b)) == BOOT_OK);
    assert(boot_block_read(&v, 499, b, sizeof(b)) == BOOT_OK);
    assert(!memcmp(a, b, sizeof(a)));
    assert(boot_block_read(&v, v.size - 1, b, 2) == BOOT_E_INVALID);
    boot_host_block_close(&hosts[0]);
    assert(boot_block_read(&tail, last_at, last, 1) == BOOT_E_STALE);
    assert(boot_volume_reset(&storage) == BOOT_OK);
    assert(boot_block_read(&v, 0, b, 1) == BOOT_E_STALE);
    assert(boot_file_read(&file, 0, b, 1, &(size_t){0}) == BOOT_E_STALE);
    /* Reuse the same session and provider slots to catch retained pointers. */
    assert(boot_diskfilter_scan(&storage) == BOOT_OK);
    assert(boot_volume_reset(&storage) == BOOT_OK);
    for (int i = 4; i < argc; ++i)
        boot_host_block_close(&hosts[i - 4]);
    return 0;
}
