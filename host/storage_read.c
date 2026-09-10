/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <boot/host_block.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct boot_storage storage;
static struct boot_fs fs;
static boot_status_t list(void *opaque, const char *name, const struct boot_file *f)
{
    (void)opaque;
    printf("%c %llu %s\n", f->directory ? 'd' : 'f', (unsigned long long)f->size, name);
    return BOOT_OK;
}
int main(int argc, char **argv)
{
    if (argc < 4 || argc > 6)
    {
        fprintf(stderr, "usage: boot-storage-read IMAGE BLOCK_SIZE PATH [PARTITION] [OFFSET]\n");
        return 2;
    }
    struct boot_host_block host = {.fd = -1};
    struct boot_slice slice;
    boot_status_t s = boot_storage_rescan(&storage);
    if (!s)
        s = boot_host_block_open(&storage, &host, argv[1], (uint32_t)strtoul(argv[2], NULL, 0),
                                 &slice);
    if (!s && argc >= 5 && strtoul(argv[4], NULL, 0))
    {
        struct boot_slice parts[BOOT_PARTITIONS];
        size_t count = BOOT_PARTITIONS;
        s = boot_partitions(&slice, parts, &count);
        if (!s)
        {
            s = BOOT_E_NOT_FOUND;
            for (size_t i = 0; i < count; ++i)
                if (parts[i].number == strtoul(argv[4], NULL, 0))
                {
                    slice = parts[i];
                    s = BOOT_OK;
                    break;
                }
        }
    }
    if (!s)
        s = boot_fs_mount(&fs, &slice);
    struct boot_file file;
    if (!s)
        s = boot_file_open(&fs, argv[3], &file);
    if (!s && file.directory)
        s = boot_file_list(&file, list, NULL);
    else if (!s)
    {
        uint8_t buffer[65536];
        uint64_t off = argc == 6 ? strtoull(argv[5], NULL, 0) : 0;
        if (off > file.size)
            s = BOOT_E_INVALID;
        while (!s && off < file.size)
        {
            size_t got = 0;
            s = boot_file_read(&file, off, buffer, sizeof(buffer), &got);
            if (!s && (!got || fwrite(buffer, 1, got, stdout) != got))
                s = BOOT_E_IO;
            off += got;
        }
    }
    boot_host_block_close(&host);
    if (s)
        fprintf(stderr, "storage status=%u\n", s);
    return s ? 1 : 0;
}
