/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_ARCHIVE_H
#define BOOT_ARCHIVE_H
#include <boot/status.h>
#include <stddef.h>
#define BOOT_ARCHIVE_MAX_SIZE (16u * 1024u * 1024u)
#define BOOT_ARCHIVE_MAX_FILES 128
struct boot_archive_file
{
    const char *name;
    const uint8_t *data;
    size_t size;
    uint32_t mode;
};
/* Views borrow immutable archive memory, which must remain alive. */
struct boot_archive
{
    struct boot_archive_file files[BOOT_ARCHIVE_MAX_FILES];
    size_t count;
};
boot_status_t boot_archive_open(struct boot_archive *, const void *, size_t);
boot_status_t boot_archive_find(const struct boot_archive *, const char *,
                                struct boot_archive_file *);
boot_status_t boot_archive_verify(const struct boot_archive *);
void boot_sha256(const void *, size_t, uint8_t[32]);
#endif
