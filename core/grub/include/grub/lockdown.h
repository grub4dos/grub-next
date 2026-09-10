/* SPDX-License-Identifier: GPL-3.0-or-later */
/* No verified/Secure Boot mode exists in this phase. */
static inline int grub_is_lockdown(void)
{
    return 0;
}
