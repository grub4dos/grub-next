/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_HOST_BLOCK_H
#define BOOT_HOST_BLOCK_H
#include <boot/storage.h>
struct boot_host_block
{
    int fd;
    uint32_t block_size;
    uint64_t size;
};
boot_status_t boot_host_block_open(struct boot_storage *, struct boot_host_block *, const char *,
                                   uint32_t, struct boot_slice *);
void boot_host_block_close(struct boot_host_block *);
#endif
