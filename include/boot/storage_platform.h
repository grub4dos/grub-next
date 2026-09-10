/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_STORAGE_PLATFORM_H
#define BOOT_STORAGE_PLATFORM_H
#include <boot/storage.h>
struct boot_context;
boot_status_t boot_platform_storage_scan(struct boot_context *, struct boot_storage *);
void boot_storage_probe(struct boot_context *);
#endif
