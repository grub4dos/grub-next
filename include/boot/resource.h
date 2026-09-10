/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_RESOURCE_H
#define BOOT_RESOURCE_H
#include <boot/archive.h>
#include <boot/context.h>
/* Input wins over embedded fallback; malformed input is never silently ignored.
   Call after input reservations and BIOS physical/bounce initialization. */
boot_status_t boot_resource_open(struct boot_context *, struct boot_archive *);
#endif
