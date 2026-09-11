/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_GRUB_EXTCMD_H
#define BOOT_GRUB_EXTCMD_H
#include <grub/command.h>
struct grub_arg_option
{
    const char *name;
    int key, flags;
    const char *doc;
    int arg, type;
};
struct grub_arg_list
{
    int set;
};
typedef struct grub_extcmd_context
{
    struct grub_arg_list *state;
} *grub_extcmd_context_t;
typedef grub_err_t (*grub_extcmd_t)(grub_extcmd_context_t, int, char **);
grub_extcmd_t grub_register_extcmd(const char *, grub_extcmd_t, unsigned, const char *,
                                   const char *, const struct grub_arg_option *);
void grub_unregister_extcmd(grub_extcmd_t);
#endif
