/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_GRUB_COMMAND_H
#define BOOT_GRUB_COMMAND_H
#include <grub/err.h>
typedef void *grub_command_t;
/* No command interpreter is imported. Only loopback's typed callback is used. */
static inline grub_command_t grub_register_command(const char *n,
                                                   grub_err_t (*f)(grub_command_t, int, char **),
                                                   const char *s, const char *d)
{
    (void)n;
    (void)f;
    (void)s;
    (void)d;
    return 0;
}
static inline void grub_unregister_command(grub_command_t c)
{
    (void)c;
}
#endif
