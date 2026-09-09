/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_PHYSICAL_H
#define BOOT_PHYSICAL_H
#include <boot/context.h>
boot_status_t boot_bios_bounce_init(struct boot_context *);
boot_status_t boot_bios_physical_init(struct boot_context *);
boot_status_t boot_phys_read(struct boot_context *, void *, uint64_t, size_t);
boot_status_t boot_phys_write(struct boot_context *, uint64_t, const void *, size_t);
boot_status_t boot_phys_copy(struct boot_context *, uint64_t, uint64_t, uint64_t);
typedef boot_status_t (*boot_phys_chunk_fn)(void *, uint64_t, void *, size_t);
boot_status_t boot_phys_chunks(uint64_t, void *, size_t, boot_phys_chunk_fn, void *);
#endif
