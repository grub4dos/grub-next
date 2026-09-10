/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_GRUB_DL_H
#define BOOT_GRUB_DL_H
typedef void *grub_dl_t;
#define GRUB_MOD_LICENSE(x)
#define GRUB_MOD_INIT(x) void boot_grub_init_##x(grub_dl_t mod)
#define GRUB_MOD_FINI(x) void boot_grub_fini_##x(void)
static inline void grub_dl_ref(grub_dl_t mod)
{
    (void)mod;
}
static inline void grub_dl_unref(grub_dl_t mod)
{
    (void)mod;
}
#endif
