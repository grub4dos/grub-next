/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_VOLUME_H
#define BOOT_VOLUME_H
#include <boot/storage.h>
/* One storage session, polling only. The session owns copied backing handles.
 * Rescan invalidates all exported handles and releases the session lazily.
 * Names are boot aliases, never stable identities. No detach/replacement API. */
boot_status_t boot_loopback_add(const char *, const struct boot_file *, struct boot_slice *);
boot_status_t boot_diskfilter_scan(struct boot_storage *);
boot_status_t boot_volume_open(struct boot_storage *, const char *, struct boot_slice *);
typedef boot_status_t (*boot_volume_hook)(void *, const char *);
boot_status_t boot_volume_list(struct boot_storage *, boot_volume_hook, void *);
/* Reset first invalidates handles, then drops the complete storage session. */
boot_status_t boot_volume_reset(struct boot_storage *);
/* Conservative: file and assembled-volume providers require materialization
 * before physical export. A slice cannot make them directly readable. */
boot_status_t boot_slice_physical(const struct boot_slice *, int *);
#endif
