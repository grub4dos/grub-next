/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Bounded, deterministic metadata mutation of generated real-format media. */
#include <assert.h>
#include <boot/storage.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct boot_storage storage;
static struct boot_fs fs;
static uint8_t *image;
static size_t image_size;
static unsigned budget;
static uint32_t bs;
static boot_status_t valid(void *p)
{
    (void)p;
    if (!budget)
        return BOOT_E_TIMEOUT;
    --budget;
    return BOOT_OK;
}
static boot_status_t read_blocks(void *p, uint64_t lba, uint32_t count, void *buffer)
{
    (void)p;
    uint64_t off = lba * bs, size = (uint64_t)count * bs;
    if (off > image_size || size > image_size - off)
        return BOOT_E_IO;
    memcpy(buffer, image + off, (size_t)size);
    return BOOT_OK;
}
static boot_status_t visit(void *p, const char *name, const struct boot_file *f)
{
    (void)p;
    (void)name;
    uint8_t out[37];
    size_t got;
    if (f->directory || !f->size)
        return BOOT_OK;
    boot_status_t s = boot_file_read(f, 0, out, sizeof(out), &got);
    if (!s)
        s = boot_file_read(f, f->size > sizeof(out) ? f->size - sizeof(out) : 0, out, sizeof(out),
                           &got);
    return s;
}
int main(int argc, char **argv)
{
    assert(argc == 3);
    bs = (uint32_t)strtoul(argv[2], NULL, 0);
    FILE *file = fopen(argv[1], "rb");
    assert(file);
    assert(!fseek(file, 0, SEEK_END));
    long length = ftell(file);
    assert(length > 0);
    rewind(file);
    image_size = (size_t)length;
    image = malloc(image_size);
    assert(image);
    assert(fread(image, 1, image_size, file) == image_size);
    fclose(file);
    const struct boot_block_ops ops = {read_blocks, valid};
    struct boot_block d = {.ops = &ops,
                           .blocks = image_size / bs,
                           .logical_size = bs,
                           .physical_size = bs,
                           .io_alignment = 1,
                           .max_blocks = 1,
                           .provider = "mutator"};
    uint32_t random = 0x4b1d;
    struct boot_slice slice, parts[BOOT_PARTITIONS];
    for (unsigned iteration = 0; iteration < 2000; ++iteration)
    {
        size_t offsets[4];
        uint8_t saved[4];
        for (unsigned j = 0; j < 4; ++j)
        {
            random = random * 1664525 + 1013904223;
            offsets[j] = random % (image_size < 4 * 1024 * 1024 ? image_size : 4 * 1024 * 1024);
            if (iteration < 256)
                offsets[j] = (iteration * 4 + j) % image_size;
            saved[j] = image[offsets[j]];
            image[offsets[j]] ^= (uint8_t)(1 + (random >> 24));
        }
        budget = 512;
        assert(!boot_storage_rescan(&storage));
        assert(!boot_block_add(&storage, &d, &slice));
        if (!boot_fs_mount(&fs, &slice))
            (void)boot_file_list(&fs.root, visit, NULL);
        size_t count = BOOT_PARTITIONS;
        (void)boot_partitions(&slice, parts, &count);
        for (unsigned j = 4; j--;)
            image[offsets[j]] = saved[j];
    }
    free(image);
    puts("PASS: 2000 deterministic media mutations, bounded I/O");
    return 0;
}
