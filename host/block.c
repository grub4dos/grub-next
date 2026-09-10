/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include <boot/host_block.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
static boot_status_t valid(void *opaque)
{
    struct boot_host_block *h = opaque;
    struct stat st;
    return h->fd < 0 || fstat(h->fd, &st) || st.st_size < 0 || (uint64_t)st.st_size != h->size
               ? BOOT_E_STALE
               : BOOT_OK;
}
static boot_status_t read_blocks(void *opaque, uint64_t lba, uint32_t count, void *buffer)
{
    struct boot_host_block *h = opaque;
    uint64_t off = lba * h->block_size;
    size_t size = (size_t)count * h->block_size;
    uint8_t *p = buffer;
    if (off > h->size || size > h->size - off || off > INT64_MAX)
        return BOOT_E_INVALID;
    while (size)
    {
        ssize_t n = pread(h->fd, p, size, (off_t)off);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return BOOT_E_IO;
        p += n;
        off += (uint64_t)n;
        size -= (size_t)n;
    }
    return BOOT_OK;
}
boot_status_t boot_host_block_open(struct boot_storage *s, struct boot_host_block *h,
                                   const char *path, uint32_t bs, struct boot_slice *v)
{
    static const struct boot_block_ops ops = {read_blocks, valid};
    if (!s || !h || !path || !v || bs < 512 || bs > 4096 || (bs & (bs - 1)))
        return BOOT_E_INVALID;
    h->fd = open(path, O_RDONLY);
    if (h->fd < 0)
        return BOOT_E_IO;
    struct stat st;
    if (fstat(h->fd, &st) || st.st_size <= 0 || st.st_size % bs)
    {
        boot_host_block_close(h);
        return BOOT_E_INVALID;
    }
    h->size = (uint64_t)st.st_size;
    h->block_size = bs;
    struct boot_block d = {.ops = &ops,
                           .opaque = h,
                           .blocks = h->size / bs,
                           .logical_size = bs,
                           .physical_size = bs,
                           .io_alignment = 1,
                           .max_blocks = 128,
                           .provider = "host-file"};
    boot_status_t result = boot_block_add(s, &d, v);
    if (result)
        boot_host_block_close(h);
    return result;
}
void boot_host_block_close(struct boot_host_block *h)
{
    if (h && h->fd >= 0)
    {
        close(h->fd);
        h->fd = -1;
    }
}
